const sendForm = document.getElementById("send-form");
const messageInput = document.getElementById("message-input");
const hopInput = document.getElementById("hop-input");
const hopValue = document.getElementById("hop-value");
const sendButton = document.getElementById("send-button");
const statusCard = document.getElementById("status-card");
const statusLabel = document.getElementById("status-label");
const statusMeta = document.getElementById("status-meta");
const deliveredMessage = document.getElementById("delivered-message");
const routeTrack = document.getElementById("route-track");
const logList = document.getElementById("log-list");

let lastSeq = 0;
let pollingHandle = null;

hopInput.addEventListener("input", () => {
  hopValue.textContent = hopInput.value;
});

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function setStatus(status, metaText) {
  statusCard.classList.remove("idle", "sending", "in_transit", "delivered", "error");
  statusCard.classList.add(status || "idle");

  const map = {
    idle: "Idle",
    sending: "Sending...",
    in_transit: "In Transit...",
    delivered: "Delivered",
    error: "Error",
  };

  statusLabel.textContent = map[status] || "Idle";
  statusMeta.textContent = metaText || "";
}

function renderRoute(route, activeNode) {
  routeTrack.innerHTML = "";

  if (!route || route.length === 0) {
    const empty = document.createElement("div");
    empty.className = "route-empty";
    empty.textContent = "Route will appear after sending.";
    routeTrack.appendChild(empty);
    return;
  }

  route.forEach((node, index) => {
    const nodeEl = document.createElement("div");
    nodeEl.className = "route-node";
    nodeEl.textContent = node;

    if (activeNode && activeNode.toLowerCase() === String(node).toLowerCase()) {
      nodeEl.classList.add("active");
    }

    routeTrack.appendChild(nodeEl);

    if (index < route.length - 1) {
      const arrow = document.createElement("span");
      arrow.className = "route-arrow";
      arrow.textContent = "→";
      routeTrack.appendChild(arrow);
    }
  });
}

function appendLogs(logs) {
  for (const entry of logs) {
    const row = document.createElement("div");
    const levelClass = String(entry.level || "INFO").toLowerCase();
    row.className = `log-row ${levelClass}`;

    const time = new Date(entry.timestamp);
    const hh = String(time.getHours()).padStart(2, "0");
    const mm = String(time.getMinutes()).padStart(2, "0");
    const ss = String(time.getSeconds()).padStart(2, "0");

    row.innerHTML =
      `<span class="log-time">${hh}:${mm}:${ss}</span>` +
      `<span class="log-source">[${escapeHtml(entry.source)}]</span>` +
      `<span class="log-message">${escapeHtml(entry.message)}</span>`;

    logList.appendChild(row);
  }

  if (logs.length > 0) {
    logList.scrollTop = logList.scrollHeight;
  }

  while (logList.childElementCount > 800) {
    logList.removeChild(logList.firstChild);
  }
}

async function fetchState() {
  try {
    const response = await fetch(`/api/state?since=${lastSeq}`);
    if (!response.ok) {
      return;
    }

    const data = await response.json();

    if (Array.isArray(data.logs) && data.logs.length > 0) {
      appendLogs(data.logs);
    }

    lastSeq = Number(data.lastSeq || lastSeq);

    const meta = data.activeNode
      ? `Active: ${data.activeNode}`
      : data.infrastructureReady
      ? "Infrastructure ready"
      : "Waiting for first send";

    setStatus(data.status || "idle", meta);
    renderRoute(data.route || [], data.activeNode || null);

    if (data.deliveredMessage) {
      deliveredMessage.textContent = `Final payload delivered: ${data.deliveredMessage}`;
    } else {
      deliveredMessage.textContent = "";
    }

    if (data.status !== "sending" && data.status !== "in_transit") {
      sendButton.disabled = false;
    }
  } catch (error) {
    setStatus("error", "Unable to reach bridge server");
  }
}

sendForm.addEventListener("submit", async (event) => {
  event.preventDefault();

  const payload = {
    message: messageInput.value.trim(),
    hops: Number(hopInput.value),
  };

  if (!payload.message) {
    return;
  }

  sendButton.disabled = true;
  logList.innerHTML = "";
  lastSeq = 0;
  deliveredMessage.textContent = "";
  setStatus("sending", "Submitting request to backend...");
  renderRoute(["Client"], "Client");

  try {
    const response = await fetch("/api/send", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify(payload),
    });

    const data = await response.json();

    if (!response.ok || !data.ok) {
      appendLogs([
        {
          timestamp: new Date().toISOString(),
          level: "ERROR",
          source: "SYSTEM",
          message: data.error || "Failed to send message",
        },
      ]);
      sendButton.disabled = false;
      setStatus("error", "Send request failed");
      return;
    }

    if (data.requestedHops !== data.effectiveHops) {
      appendLogs([
        {
          timestamp: new Date().toISOString(),
          level: "WARN",
          source: "SYSTEM",
          message: `Hop request adjusted: ${data.requestedHops} -> ${data.effectiveHops}`,
        },
      ]);
    }

    setStatus("sending", "Client wrapping onion layers...");
  } catch (error) {
    appendLogs([
      {
        timestamp: new Date().toISOString(),
        level: "ERROR",
        source: "SYSTEM",
        message: "Bridge server request failed",
      },
    ]);
    sendButton.disabled = false;
    setStatus("error", "Bridge server request failed");
  }
});

function startPolling() {
  if (pollingHandle) {
    clearInterval(pollingHandle);
  }

  fetchState();
  pollingHandle = setInterval(fetchState, 650);
}

startPolling();
