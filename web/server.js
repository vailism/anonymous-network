const express = require("express");
const fs = require("fs");
const path = require("path");
const { spawn } = require("child_process");

const app = express();
const PORT = process.env.PORT || 8080;
const STDBUF_PATH = fs.existsSync("/usr/bin/stdbuf") ? "/usr/bin/stdbuf" : null;

const ROOT_DIR = path.resolve(__dirname, "..");
const BUILD_DIR = path.join(ROOT_DIR, "build");
const CONFIG_PATH = path.join(ROOT_DIR, "config", "network.conf");

const state = {
  status: "idle",
  route: [],
  activeNode: null,
  deliveredMessage: null,
  logs: [],
  seq: 0,
  infrastructureReady: false,
  relayCount: 0,
  effectiveHopCount: 3,
};

const runtime = {
  networkConfig: null,
  endpointAlias: new Map(),
  processes: new Map(),
  startupPromise: null,
  sendInFlight: false,
  shuttingDown: false,
};

app.use(express.json());
app.use(express.static(path.join(__dirname, "public")));

function parseEndpoint(value) {
  const parts = String(value || "").trim().split(":");
  if (parts.length !== 2) {
    throw new Error(`Invalid endpoint: ${value}`);
  }

  const host = parts[0];
  const port = Number(parts[1]);
  if (!host || !Number.isInteger(port) || port <= 0 || port > 65535) {
    throw new Error(`Invalid endpoint: ${value}`);
  }

  return { host, port, text: `${host}:${port}` };
}

function indexToLabel(index) {
  const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  if (index < alphabet.length) {
    return alphabet[index];
  }
  return `N${index + 1}`;
}

function loadConfig(configPath) {
  if (!fs.existsSync(configPath)) {
    throw new Error(`Missing config file at ${configPath}`);
  }

  const lines = fs.readFileSync(configPath, "utf8").split(/\r?\n/);
  const map = new Map();

  for (const originalLine of lines) {
    const line = originalLine.trim();
    if (!line || line.startsWith("#")) {
      continue;
    }

    const separator = line.indexOf("=");
    if (separator === -1) {
      continue;
    }

    const key = line.slice(0, separator).trim();
    const value = line.slice(separator + 1).trim();
    map.set(key, value);
  }

  if (!map.has("server")) {
    throw new Error("Config is missing server endpoint");
  }

  const serverEndpoint = parseEndpoint(map.get("server"));

  const relays = [];
  for (let i = 1; ; i += 1) {
    const endpointKey = `relay${i}.endpoint`;
    const relayKey = `relay${i}.key`;

    if (!map.has(endpointKey) && !map.has(relayKey)) {
      break;
    }

    if (!map.has(endpointKey) || !map.has(relayKey)) {
      throw new Error(`Config relay${i} entry is incomplete`);
    }

    const endpoint = parseEndpoint(map.get(endpointKey));
    const key = map.get(relayKey);
    if (!key) {
      throw new Error(`Config relay${i}.key cannot be empty`);
    }

    const label = indexToLabel(i - 1);
    relays.push({
      id: `relay-${label}`,
      label,
      displayName: `Node ${label}`,
      componentName: `NODE-${label}`,
      endpoint,
      key,
    });
  }

  if (relays.length === 0) {
    throw new Error("Config must define at least one relay");
  }

  return {
    server: {
      displayName: "Server",
      componentName: "FINAL-SERVER",
      endpoint: serverEndpoint,
    },
    relays,
  };
}

function appendLog({ source, level = "INFO", message }) {
  const entry = {
    seq: state.seq + 1,
    timestamp: new Date().toISOString(),
    source,
    level,
    message,
  };

  state.seq = entry.seq;
  state.logs.push(entry);
  if (state.logs.length > 3000) {
    state.logs.shift();
  }

  updateStateFromLog(entry);
}

function appendSystemLog(message, level = "INFO") {
  appendLog({ source: "SYSTEM", level, message });
}

function parseStructuredLogLine(rawLine, fallbackSource) {
  const line = String(rawLine || "").trim();
  if (!line) {
    return null;
  }

  const match = line.match(/^\[[^\]]+\]\s+\[(INFO|WARN|ERROR)\]\s+\[([^\]]+)\]\s+(.*)$/);
  if (match) {
    return {
      level: match[1],
      source: match[2],
      message: match[3],
    };
  }

  return {
    level: "INFO",
    source: fallbackSource,
    message: line,
  };
}

function mapEndpointToDisplay(endpointText) {
  return runtime.endpointAlias.get(endpointText) || endpointText;
}

function sourceToDisplay(source) {
  if (source === "FINAL-SERVER") {
    return "Server";
  }
  if (/^NODE-/.test(source)) {
    return source.replace("NODE-", "Node ");
  }
  if (source === "CLIENT") {
    return "Client";
  }
  return source;
}

function updateStateFromLog(entry) {
  if (entry.level === "ERROR" && runtime.sendInFlight) {
    state.status = "error";
  }

  if (entry.source === "CLIENT") {
    const hopMatch = entry.message.match(/hop\s+\d+:\s+([0-9.]+:[0-9]+)/i);
    if (hopMatch) {
      const hopDisplay = mapEndpointToDisplay(hopMatch[1]);
      if (!state.route.includes(hopDisplay)) {
        state.route.push(hopDisplay);
      }
    }

    const finalMatch = entry.message.match(/final:\s+([0-9.]+:[0-9]+)/i);
    if (finalMatch) {
      const finalDisplay = mapEndpointToDisplay(finalMatch[1]);
      if (!state.route.includes(finalDisplay)) {
        state.route.push(finalDisplay);
      }
    }

    if (entry.message.includes("Anonymous message dispatched")) {
      state.status = "in_transit";
      state.activeNode = state.route[1] || "Client";
    }
  }

  if (/^NODE-/.test(entry.source) && runtime.sendInFlight) {
    state.status = "in_transit";
    state.activeNode = sourceToDisplay(entry.source);
  }

  if (entry.source === "FINAL-SERVER") {
    if (entry.message.includes("Incoming connection") && runtime.sendInFlight) {
      state.status = "in_transit";
      state.activeNode = "Server";
    }

    const deliveryMatch = entry.message.match(/Final plaintext received:\s+'(.*)'/);
    if (deliveryMatch) {
      state.status = "delivered";
      state.activeNode = "Server";
      state.deliveredMessage = deliveryMatch[1];
      runtime.sendInFlight = false;
    }
  }
}

function wireProcessOutput(processState) {
  function attach(stream, isError) {
    stream.setEncoding("utf8");
    stream.on("data", (chunk) => {
      const key = isError ? "stderrBuffer" : "stdoutBuffer";
      processState[key] += chunk;

      const lines = processState[key].split(/\r?\n/);
      processState[key] = lines.pop();

      for (const line of lines) {
        const parsed = parseStructuredLogLine(line, processState.source);
        if (parsed) {
          appendLog(parsed);
        }
      }
    });
  }

  attach(processState.child.stdout, false);
  attach(processState.child.stderr, true);
}

function startProcess(id, source, command, args) {
  if (runtime.processes.has(id)) {
    return runtime.processes.get(id);
  }

  const wrapped = wrapCommandWithLineBuffer(command, args);

  const child = spawn(wrapped.command, wrapped.args, {
    cwd: ROOT_DIR,
    stdio: ["ignore", "pipe", "pipe"],
  });

  const processState = {
    id,
    source,
    command,
    args,
    child,
    stdoutBuffer: "",
    stderrBuffer: "",
  };

  runtime.processes.set(id, processState);
  wireProcessOutput(processState);

  child.on("exit", (code, signal) => {
    runtime.processes.delete(id);

    if (processState.stdoutBuffer.trim()) {
      const parsed = parseStructuredLogLine(processState.stdoutBuffer, source);
      if (parsed) {
        appendLog(parsed);
      }
    }

    if (processState.stderrBuffer.trim()) {
      const parsed = parseStructuredLogLine(processState.stderrBuffer, source);
      if (parsed) {
        appendLog(parsed);
      }
    }

    if (!runtime.shuttingDown) {
      appendSystemLog(
        `${source} exited unexpectedly (code=${code === null ? "null" : code}, signal=${signal || "none"})`,
        "WARN"
      );
      state.infrastructureReady = false;
      if (runtime.sendInFlight) {
        state.status = "error";
      }
    }
  });

  return processState;
}

function stopAllProcesses() {
  runtime.shuttingDown = true;

  for (const processState of runtime.processes.values()) {
    processState.child.kill("SIGTERM");
  }

  runtime.processes.clear();
  state.infrastructureReady = false;
  runtime.sendInFlight = false;
}

function wrapCommandWithLineBuffer(command, args) {
  if (!STDBUF_PATH) {
    return { command, args };
  }

  return {
    command: STDBUF_PATH,
    args: ["-oL", "-eL", command, ...args],
  };
}

function verifyBinariesExist() {
  const required = ["anon_server", "anon_node", "anon_client"].map((bin) => path.join(BUILD_DIR, bin));

  for (const bin of required) {
    if (!fs.existsSync(bin)) {
      throw new Error(`Missing binary: ${bin}. Build the C++ backend first.`);
    }
  }
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function ensureInfrastructure() {
  if (state.infrastructureReady) {
    return;
  }

  if (runtime.startupPromise) {
    return runtime.startupPromise;
  }

  runtime.startupPromise = (async () => {
    verifyBinariesExist();

    appendSystemLog("Starting onion-routing infrastructure...");

    const serverBin = path.join(BUILD_DIR, "anon_server");
    const nodeBin = path.join(BUILD_DIR, "anon_node");

    startProcess(
      "final-server",
      runtime.networkConfig.server.componentName,
      serverBin,
      [String(runtime.networkConfig.server.endpoint.port)]
    );

    await delay(150);

    for (const relay of runtime.networkConfig.relays) {
      startProcess(relay.id, relay.componentName, nodeBin, [
        relay.label,
        String(relay.endpoint.port),
        relay.key,
        "20",
        "120",
      ]);
      await delay(90);
    }

    await delay(600);
    state.infrastructureReady = true;
    appendSystemLog("Infrastructure started and ready.");
  })();

  try {
    await runtime.startupPromise;
  } finally {
    runtime.startupPromise = null;
  }
}

function resetSessionForSend() {
  state.status = "sending";
  state.route = ["Client"];
  state.activeNode = "Client";
  state.deliveredMessage = null;
  state.logs = [];
  state.seq = 0;
}

function spawnClientMessage(message, hopCount) {
  return new Promise((resolve, reject) => {
    const clientBin = path.join(BUILD_DIR, "anon_client");
    const args = [path.join("config", "network.conf"), message, String(hopCount)];
    const wrapped = wrapCommandWithLineBuffer(clientBin, args);

    const child = spawn(wrapped.command, wrapped.args, {
      cwd: ROOT_DIR,
      stdio: ["ignore", "pipe", "pipe"],
    });

    let stdoutBuffer = "";
    let stderrBuffer = "";

    function consumeChunk(chunk, isError) {
      if (isError) {
        stderrBuffer += chunk;
      } else {
        stdoutBuffer += chunk;
      }

      const activeBuffer = isError ? stderrBuffer : stdoutBuffer;
      const lines = activeBuffer.split(/\r?\n/);
      const tail = lines.pop();

      if (isError) {
        stderrBuffer = tail;
      } else {
        stdoutBuffer = tail;
      }

      for (const line of lines) {
        const parsed = parseStructuredLogLine(line, "CLIENT");
        if (parsed) {
          appendLog(parsed);
        }
      }
    }

    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");

    child.stdout.on("data", (chunk) => consumeChunk(chunk, false));
    child.stderr.on("data", (chunk) => consumeChunk(chunk, true));

    child.on("close", (code) => {
      if (stdoutBuffer.trim()) {
        const parsed = parseStructuredLogLine(stdoutBuffer, "CLIENT");
        if (parsed) {
          appendLog(parsed);
        }
      }

      if (stderrBuffer.trim()) {
        const parsed = parseStructuredLogLine(stderrBuffer, "CLIENT");
        if (parsed) {
          appendLog(parsed);
        }
      }

      if (code !== 0) {
        reject(new Error(`anon_client exited with code ${code}`));
        return;
      }

      resolve();
    });
  });
}

app.get("/api/health", (req, res) => {
  res.json({
    ok: true,
    infrastructureReady: state.infrastructureReady,
    status: state.status,
    relayCount: state.relayCount,
  });
});

app.get("/api/state", (req, res) => {
  const sinceRaw = Number(req.query.since || 0);
  const since = Number.isFinite(sinceRaw) ? sinceRaw : 0;

  const logs = since > 0 ? state.logs.filter((entry) => entry.seq > since) : state.logs.slice(-350);

  res.json({
    status: state.status,
    route: state.route,
    activeNode: state.activeNode,
    deliveredMessage: state.deliveredMessage,
    infrastructureReady: state.infrastructureReady,
    relayCount: state.relayCount,
    effectiveHopCount: state.effectiveHopCount,
    logs,
    lastSeq: state.seq,
    sendInFlight: runtime.sendInFlight,
  });
});

app.post("/api/send", async (req, res) => {
  if (runtime.sendInFlight) {
    res.status(409).json({ ok: false, error: "A message is already in transit." });
    return;
  }

  const message = String(req.body?.message || "").trim();
  if (!message) {
    res.status(400).json({ ok: false, error: "Message cannot be empty." });
    return;
  }

  const requestedHopsRaw = Number(req.body?.hops || 3);
  let requestedHops = Number.isFinite(requestedHopsRaw) ? Math.trunc(requestedHopsRaw) : 3;
  requestedHops = Math.max(1, Math.min(5, requestedHops));

  let effectiveHops = requestedHops;
  if (effectiveHops < 3) {
    effectiveHops = 3;
  }

  if (effectiveHops > runtime.networkConfig.relays.length) {
    effectiveHops = runtime.networkConfig.relays.length;
  }

  try {
    await ensureInfrastructure();
  } catch (error) {
    state.status = "error";
    res.status(500).json({ ok: false, error: error.message });
    return;
  }

  resetSessionForSend();
  state.effectiveHopCount = effectiveHops;
  runtime.sendInFlight = true;

  appendSystemLog(`Preparing anonymous send for message: "${message}"`);
  if (requestedHops !== effectiveHops) {
    appendSystemLog(
      `Requested ${requestedHops} hops; using ${effectiveHops} based on backend constraints.`,
      "WARN"
    );
  }

  spawnClientMessage(message, effectiveHops)
    .then(async () => {
      if (state.status === "sending") {
        state.status = "in_transit";
      }

      // Give relays/server time to flush logs before marking timeout.
      await delay(6000);

      if (runtime.sendInFlight && state.status !== "delivered") {
        runtime.sendInFlight = false;
        state.status = "error";
        appendSystemLog("Message send timed out before delivery confirmation.", "WARN");
      }
    })
    .catch((error) => {
      runtime.sendInFlight = false;
      state.status = "error";
      appendSystemLog(`Client send failed: ${error.message}`, "ERROR");
    });

  res.json({
    ok: true,
    requestedHops,
    effectiveHops,
  });
});

function initialize() {
  runtime.networkConfig = loadConfig(CONFIG_PATH);

  runtime.endpointAlias.clear();
  runtime.endpointAlias.set(runtime.networkConfig.server.endpoint.text, "Server");

  for (const relay of runtime.networkConfig.relays) {
    runtime.endpointAlias.set(relay.endpoint.text, relay.displayName);
  }

  state.relayCount = runtime.networkConfig.relays.length;
  appendSystemLog("Web bridge initialized. Waiting for first send request.");
}

process.on("SIGINT", () => {
  stopAllProcesses();
  process.exit(0);
});

process.on("SIGTERM", () => {
  stopAllProcesses();
  process.exit(0);
});

initialize();

app.listen(PORT, () => {
  // Keep startup logs simple for terminal operators.
  console.log(`Web dashboard running on http://localhost:${PORT}`);
  console.log(`Project root: ${ROOT_DIR}`);
});
