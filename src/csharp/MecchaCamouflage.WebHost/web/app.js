const pending = new Map();
const hotkeyKeys = [
  "app.startHotkey",
  "app.previewHotkey",
  "app.unpreviewHotkey",
  "app.stopHotkey"
];

let liveSnapshot = null;
let draftSnapshot = null;
let editing = false;
let activeLogFilter = "all";
let recordingHotkey = null;
let lastRenderedLogValue = null;

window.chrome.webview.addEventListener("message", event => {
  const message = event.data;
  if (message.type === "response") {
    const entry = pending.get(message.id);
    if (entry) {
      pending.delete(message.id);
      message.ok ? entry.resolve(message.data) : entry.reject(message.data);
    }
    return;
  }
  if (message.type === "event" && message.name === "snapshotChanged") {
    liveSnapshot = message.data;
    render();
    return;
  }
  if (message.type === "event" && message.name === "toast") {
    toast(message.data.message, message.data.level || "success");
  }
});

function send(command, payload = {}) {
  const id = crypto.randomUUID();
  const promise = new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
  window.chrome.webview.postMessage({ id, command, payload });
  return promise;
}

function byId(id) {
  return document.getElementById(id);
}

function text(id, value) {
  byId(id).textContent = value;
}

function setValue(id, next) {
  const element = byId(id);
  if (document.activeElement !== element) {
    element.value = next;
  }
}

function setChecked(id, next) {
  byId(id).checked = Boolean(next);
}

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function fmt(value) {
  return Number(value).toFixed(2).replace(/\.?0+$/, "");
}

function activeLocale() {
  return currentSnapshot()?.language || liveSnapshot?.language || "en";
}

function translationsFor(locale) {
  const translations = liveSnapshot?.translations || {};
  return translations[locale] || translations.en || {};
}

function i18n(key, ...args) {
  const locale = activeLocale();
  const local = translationsFor(locale);
  const english = translationsFor("en");
  let value = local[key] || english[key] || key;
  args.forEach((arg, index) => {
    value = value.replaceAll(`{${index}}`, arg);
  });
  return value;
}

function applyI18n() {
  for (const element of document.querySelectorAll("[data-i18n]")) {
    element.textContent = i18n(element.dataset.i18n);
  }
  for (const element of document.querySelectorAll("[data-i18n-aria-label]")) {
    element.setAttribute("aria-label", i18n(element.dataset.i18nAriaLabel));
  }
  document.title = i18n("app.title");
}

function currentSnapshot() {
  return editing && draftSnapshot ? draftSnapshot : liveSnapshot;
}

function render() {
  if (!liveSnapshot) {
    return;
  }
  applyI18n();
  renderRuntime(liveSnapshot);
  renderSettings(currentSnapshot());
  applyI18n();
  renderEditState();
}

function renderRuntime(snapshot) {
  const runtime = snapshot.runtime;
  setStatus("footer-process", runtime.process);
  setStatus("footer-bridge", runtime.bridge);
  text("version", snapshot.version);
  renderLogs(runtime);
}

function renderLogs(runtime) {
  const logs = runtime.logs || "";
  const value = logs.trim().length > 0 ? logs : "";
  if (activeLogFilter === "all") {
    const progressLine = buildProgressLine(runtime);
    setLogHtml([value, progressLine].filter(Boolean).join("\n"));
    return;
  }
  const token = `[${activeLogFilter.toUpperCase()}]`;
  const filtered = value
    .split(/\r?\n/)
    .filter(line => line.toUpperCase().includes(token))
    .join("\n");
  setLogHtml(filtered);
}

function buildProgressLine(runtime) {
  if (!runtime.progressVisible) {
    return "";
  }
  const percent = Math.max(0, Math.min(100, Math.round(runtime.progressPercent)));
  const timingLabel = runtime.timingLabel || "delay";
  const detail = [
    `batch ${runtime.batch || "-"}`,
    `${timingLabel} ${runtime.delay || "-"}`,
    `queue ${runtime.queue || "-"}`,
    `ETA ${runtime.paintEta || "-"}`,
    `elapsed ${runtime.paintElapsed || "-"}`
  ].join(" | ");
  return `${logPrefix("INFO")} Paint: ${percent}% ${progressBar(percent)} | ${detail}`;
}

function progressBar(percent) {
  const width = 16;
  const filled = Math.max(0, Math.min(width, Math.round((percent / 100) * width)));
  return `[${"#".repeat(filled)}${"-".repeat(width - filled)}]`;
}

function logPrefix(level) {
  const now = new Date();
  const part = value => String(value).padStart(2, "0");
  return `${part(now.getHours())}:${part(now.getMinutes())}:${part(now.getSeconds())} [${level}]`;
}

function setLogHtml(value) {
  const logs = byId("logs");
  if (value === lastRenderedLogValue) {
    return;
  }
  const stickToBottom = lastRenderedLogValue === null || (logs.scrollHeight - logs.scrollTop - logs.clientHeight) < 24;
  lastRenderedLogValue = value;
  const lines = String(value).split(/\r?\n/);
  if (lines[lines.length - 1].length > 0) {
    lines.push("");
  }
  logs.innerHTML = lines
    .map(line => `<span class="${logLineClass(line)}">&gt; ${escapeHtml(line)}</span>`)
    .join("\n");
  if (stickToBottom) {
    requestAnimationFrame(() => {
      logs.scrollTop = logs.scrollHeight;
    });
  }
}

function logLineClass(line) {
  const upper = line.toUpperCase();
  if ((upper.startsWith("PAINT: ") || /\[INFO\]\s+PAINT:\s+\d+%/.test(upper)) && upper.includes("% [")) {
    return "log-line progress";
  }
  if (upper.includes("[ERROR]")) {
    return "log-line error";
  }
  if (upper.includes("[WARN]")) {
    return "log-line warn";
  }
  return "log-line";
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function setStatus(id, value) {
  const element = byId(id);
  element.textContent = localizedStatus(value);
  element.className = `status-token ${statusClass(value)}`;
}

function localizedStatus(value) {
  const normalized = String(value || "").toLowerCase();
  return i18n(`state.${normalized}`);
}

function statusClass(value) {
  const normalized = String(value || "").toLowerCase();
  if (["attached", "connected", "ready", "running", "complete", "ok"].includes(normalized)) {
    return "ok";
  }
  if (["waiting", "starting", "pending"].includes(normalized)) {
    return "wait";
  }
  if (["failed", "error", "cancelled"].includes(normalized)) {
    return "bad";
  }
  return "idle";
}

function renderSettings(snapshot) {
  const paint = snapshot.settings.paint;
  setNumberPair("brush-size", "brush-size-number", paint.brushSizeTexels);
  setNumberPair("packed-batch-delay", "packed-batch-delay-number", paint.packedBatchDelayMs);
  setChecked("auto-material", paint.autoMaterial);
  setNumberPair("metallic", "metallic-number", paint.metallic);
  setNumberPair("roughness", "roughness-number", paint.roughness);
  renderRegionButtons(document.querySelector('[data-region="paint.frontRegionMode"]'), "paint.frontRegionMode", paint.frontRegionMode);
  renderRegionButtons(document.querySelector('[data-region="paint.sideRegionMode"]'), "paint.sideRegionMode", paint.sideRegionMode);
  renderRegionButtons(document.querySelector('[data-region="paint.backRegionMode"]'), "paint.backRegionMode", paint.backRegionMode);
  setColor(paint.fillColor);
  setNumberPair("fill-metallic", "fill-metallic-number", paint.fillMetallic);
  setNumberPair("fill-roughness", "fill-roughness-number", paint.fillRoughness);

  const app = snapshot.settings.app;
  applyThemeColor(app.themeColor);
  setChecked("always-on-top", app.alwaysOnTop);
  setNumberPair("opacity", "opacity-number", Math.round(app.opacity * 100));
  setColorPair("theme-color-picker", "theme-color", app.themeColor);
  setValue("start-hotkey", app.startHotkey);
  setValue("preview-hotkey", app.previewHotkey);
  setValue("unpreview-hotkey", app.unPreviewHotkey);
  setValue("stop-hotkey", app.stopHotkey);

  const quality = byId("quality-preset");
  if (quality.options.length === 0) {
    for (const preset of ["fast", "balanced", "high", "ultra"]) {
      const option = document.createElement("option");
      option.value = preset;
      option.dataset.i18n = "quality." + preset;
      option.textContent = i18n("quality." + preset);
      quality.append(option);
    }
  }
  setValue("quality-preset", paint.qualityPreset);

  setChecked("detection-artifacts", paint.enableDetectionArtifacts);
  const detectionDetail = byId("detection-detail");
  if (detectionDetail.options.length === 0) {
    for (const level of [1, 2, 3, 4, 5]) {
      const option = document.createElement("option");
      option.value = String(level);
      option.dataset.i18n = "detection.detail." + level;
      option.textContent = i18n("detection.detail." + level);
      detectionDetail.append(option);
    }
  }
  setValue("detection-detail", String(paint.detectionDetail));
  setChecked("local-image-enabled", paint.useLocalImageSource);
  setValue("local-image-path", paint.localImagePath || "");
  setChecked("bypass-live-capture", paint.bypassLiveCapture);

  const language = byId("language");
  if (language.options.length === 0) {
    for (const locale of liveSnapshot.locales) {
      const option = document.createElement("option");
      option.value = locale.code;
      option.textContent = locale.nativeName;
      language.append(option);
    }
  }
  setValue("language", snapshot.language);

  for (const control of document.querySelectorAll(".setting-control")) {
    control.disabled = !editing;
  }
  for (const button of document.querySelectorAll(".record-hotkey")) {
    button.disabled = !editing;
  }
  byId("select-local-image").disabled = !editing;
  setDisabled(["bypass-live-capture"], !editing || !paint.useLocalImageSource);

  const materialLocked = paint.autoMaterial || !editing;
  setDisabled(["metallic", "metallic-number", "roughness", "roughness-number"], materialLocked);

  const fillLocked = !editing || !usesFill(paint);
  byId("fill-section").classList.toggle("disabled", !usesFill(paint));
  setDisabled([
    "fill-color-picker",
    "fill-color",
    "fill-metallic",
    "fill-metallic-number",
    "fill-roughness",
    "fill-roughness-number"
  ], fillLocked);
}

function setNumberPair(sliderId, numberId, value) {
  setValue(sliderId, value);
  setValue(numberId, fmt(value));
}

function setColor(value) {
  setColorPair("fill-color-picker", "fill-color", value);
}

function setColorPair(pickerId, inputId, value) {
  const color = normalizeColor(value) || "#FFFFFF";
  setValue(pickerId, color);
  setValue(inputId, color);
}

function applyThemeColor(value) {
  const color = normalizeColor(value) || "#FFFFFF";
  document.documentElement.style.setProperty("--primary", color);
}

function setDisabled(ids, disabled) {
  for (const id of ids) {
    byId(id).disabled = disabled;
  }
}

function renderRegionButtons(container, key, current) {
  container.innerHTML = "";
  for (const mode of ["paint", "fill", "skip"]) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = i18n(`mode.${mode}`);
    button.className = mode === current ? "active" : "";
    button.disabled = !editing;
    button.addEventListener("click", () => {
      if (!editing) {
        return;
      }
      setDraftSetting(key, mode);
      renderSettings(draftSnapshot);
    });
    container.append(button);
  }
}

function renderEditState() {
  document.body.classList.toggle("editing", editing);
  byId("edit-settings").disabled = editing;
  byId("save-settings").disabled = !editing;
  byId("cancel-edit").disabled = !editing;
  byId("reset-settings").disabled = !editing;
}

function usesFill(paint) {
  return paint.frontRegionMode === "fill" || paint.sideRegionMode === "fill" || paint.backRegionMode === "fill";
}

function beginEdit() {
  if (!liveSnapshot) {
    return;
  }
  editing = true;
  draftSnapshot = clone(liveSnapshot);
  send("setEditing", { editing: true }).catch(error => showError(error.message || String(error)));
  render();
}

function cancelEdit() {
  editing = false;
  draftSnapshot = null;
  closeHotkeyDialog();
  send("setEditing", { editing: false }).catch(error => showError(error.message || String(error)));
  previewSavedWindow();
  render();
}

function resetDraft() {
  if (!editing || !liveSnapshot || !draftSnapshot) {
    return;
  }
  const currentProcessName = liveSnapshot.settings.app.processName;
  draftSnapshot.settings = clone(liveSnapshot.defaults);
  draftSnapshot.settings.app.processName = currentProcessName;
  draftSnapshot.language = liveSnapshot.language;
  render();
  previewDraftWindow();
}

async function saveDraft() {
  if (!editing || !liveSnapshot || !draftSnapshot) {
    return;
  }
  const changes = diffSnapshots(liveSnapshot, draftSnapshot);
  if (changes.length === 0) {
    cancelEdit();
    return;
  }
  const result = await send("updateSettings", { changes });
  if (!result.success) {
    showError(result.message || i18n("error.settings.not.saved"));
    document.activeElement?.blur();
    draftSnapshot = clone(liveSnapshot);
    previewSavedWindow();
    render();
    return;
  }
  editing = false;
  draftSnapshot = null;
  closeHotkeyDialog();
  await send("setEditing", { editing: false });
  toast(i18n("toast.settings.saved"));
  refresh().catch(error => showError(error.message || String(error)));
}

function previewSavedWindow() {
  if (!liveSnapshot) {
    return;
  }
  send("previewWindow", { opacity: liveSnapshot.settings.app.opacity }).catch(error => showError(error.message || String(error)));
  applyThemeColor(liveSnapshot.settings.app.themeColor);
}

function previewDraftWindow() {
  if (!draftSnapshot) {
    return;
  }
  send("previewWindow", { opacity: draftSnapshot.settings.app.opacity }).catch(error => showError(error.message || String(error)));
  applyThemeColor(draftSnapshot.settings.app.themeColor);
}

async function refresh() {
  liveSnapshot = await send("getSnapshot");
  render();
}

function setDraftSetting(key, value) {
  if (!draftSnapshot) {
    return;
  }
  if (key === "app.language") {
    draftSnapshot.language = value;
    return;
  }
  const path = snapshotPath(key);
  let node = draftSnapshot.settings;
  for (let index = 0; index < path.length - 1; ++index) {
    node = node[path[index]];
  }
  node[path.at(-1)] = value;
  if (key === "paint.brushSizeTexels") {
    draftSnapshot.settings.paint.coverageStepTexels = value;
  }
}

function getSnapshotSetting(snapshot, key) {
  if (key === "app.language") {
    return snapshot.language;
  }
  const path = snapshotPath(key);
  let node = snapshot.settings;
  for (const part of path) {
    node = node[part];
  }
  return node;
}

function snapshotPath(key) {
  if (key === "app.unpreviewHotkey") {
    return ["app", "unPreviewHotkey"];
  }
  return key.split(".");
}

function diffSnapshots(before, after) {
  const keys = [
    "app.language",
    "paint.brushSizeTexels",
    "paint.packedBatchDelayMs",
    "paint.autoMaterial",
    "paint.metallic",
    "paint.roughness",
    "paint.frontRegionMode",
    "paint.sideRegionMode",
    "paint.backRegionMode",
    "paint.fillColor",
    "paint.fillMetallic",
    "paint.fillRoughness",
    "paint.qualityPreset",
    "paint.enableDetectionArtifacts",
    "paint.detectionDetail",
    "paint.useLocalImageSource",
    "paint.localImagePath",
    "paint.bypassLiveCapture",
    "app.alwaysOnTop",
    "app.opacity",
    "app.themeColor",
    "app.startHotkey",
    "app.previewHotkey",
    "app.unpreviewHotkey",
    "app.stopHotkey"
  ];
  const changes = [];
  for (const key of keys) {
    const oldValue = getSnapshotSetting(before, key);
    const newValue = getSnapshotSetting(after, key);
    if (oldValue !== newValue) {
      changes.push({ key, value: newValue });
    }
  }
  return changes;
}

function normalizeColor(value) {
  const textValue = String(value || "").trim();
  const match = /^#?[0-9a-fA-F]{6}$/.exec(textValue);
  if (!match) {
    return null;
  }
  return ("#" + textValue.replace("#", "")).toUpperCase();
}

function bindRangePair(sliderId, numberId, key, transform = Number) {
  const slider = byId(sliderId);
  const number = byId(numberId);
  const commit = source => {
    const raw = Number(source.value);
    if (!Number.isFinite(raw)) {
      return;
    }
    const clamped = clamp(raw, Number(source.min), Number(source.max));
    slider.value = String(clamped);
    number.value = fmt(clamped);
    setDraftSetting(key, transform(clamped));
    if (key === "app.opacity") {
      send("previewWindow", { opacity: transform(clamped) }).catch(error => showError(error.message || String(error)));
    }
  };
  slider.addEventListener("input", () => commit(slider));
  number.addEventListener("change", () => commit(number));
  number.addEventListener("keydown", event => {
    if (event.key === "Enter") {
      number.blur();
    }
  });
}

function bindInput(id, key, transform = value => value) {
  const element = byId(id);
  element.addEventListener("change", () => setDraftSetting(key, transform(element.value)));
  element.addEventListener("keydown", event => {
    if (event.key === "Enter") {
      element.blur();
    }
  });
}

function bindCheckbox(id, key) {
  byId(id).addEventListener("change", event => {
    setDraftSetting(key, event.target.checked);
    renderSettings(draftSnapshot);
  });
}

function bindColorPair(pickerId, inputId, key) {
  const picker = byId(pickerId);
  const textInput = byId(inputId);
  picker.addEventListener("input", () => {
    const color = normalizeColor(picker.value);
    if (!color) {
      return;
    }
    textInput.value = color;
    setDraftSetting(key, color);
    if (key === "app.themeColor") {
      applyThemeColor(color);
    }
  });
  textInput.addEventListener("change", () => {
    const color = normalizeColor(textInput.value);
    if (!color) {
      setDraftSetting(key, textInput.value);
      return;
    }
    picker.value = color;
    textInput.value = color;
    setDraftSetting(key, color);
    if (key === "app.themeColor") {
      applyThemeColor(color);
    }
  });
  textInput.addEventListener("keydown", event => {
    if (event.key === "Enter") {
      textInput.blur();
    }
  });
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function beginHotkeyRecord(key, inputId) {
  if (!editing) {
    return;
  }
  recordingHotkey = { key, inputId };
  send("setHotkeyRecording", { recording: true }).catch(error => showError(error.message || String(error)));
  setHotkeyDialogMessage(i18n("dialog.hotkey.supported"), false);
  byId("hotkey-dialog").hidden = false;
}

function closeHotkeyDialog() {
  recordingHotkey = null;
  send("setHotkeyRecording", { recording: false }).catch(error => showError(error.message || String(error)));
  byId("hotkey-dialog").hidden = true;
}

function recordHotkeyFromEvent(event) {
  if (!recordingHotkey) {
    return;
  }
  event.preventDefault();
  if (event.key === "Escape" || event.key === "Esc") {
    closeHotkeyDialog();
    return;
  }
  const key = event.key.toUpperCase();
  if (!/^F([1-9]|1[0-9]|2[0-4])$/.test(key)) {
    toast(i18n("toast.hotkey.unsupported"), "error");
    return;
  }
  if (isDuplicateHotkey(key, recordingHotkey.key)) {
    toast(i18n("toast.hotkey.duplicate", key), "error");
    return;
  }
  setDraftSetting(recordingHotkey.key, key);
  setValue(recordingHotkey.inputId, key);
  closeHotkeyDialog();
}

function isDuplicateHotkey(value, ownKey) {
  return hotkeyKeys.some(key => key !== ownKey && getSnapshotSetting(draftSnapshot, key).toUpperCase() === value);
}

function setHotkeyDialogMessage(message, error) {
  const dialog = byId("hotkey-dialog");
  dialog.classList.toggle("error", error);
  byId("hotkey-dialog-message").textContent = message;
}

function showError(message) {
  console.error(message);
  toast(message, "error");
}

function toast(message, level = "success") {
  const toastElement = byId("toast");
  toastElement.textContent = message;
  toastElement.className = `visible ${level}`;
  clearTimeout(toastElement._timer);
  toastElement._timer = setTimeout(() => {
    toastElement.className = "";
  }, 2400);
}

document.addEventListener("DOMContentLoaded", () => {
  bindRangePair("brush-size", "brush-size-number", "paint.brushSizeTexels");
  bindRangePair("packed-batch-delay", "packed-batch-delay-number", "paint.packedBatchDelayMs");
  bindCheckbox("auto-material", "paint.autoMaterial");
  bindRangePair("metallic", "metallic-number", "paint.metallic");
  bindRangePair("roughness", "roughness-number", "paint.roughness");
  bindColorPair("fill-color-picker", "fill-color", "paint.fillColor");
  bindRangePair("fill-metallic", "fill-metallic-number", "paint.fillMetallic");
  bindRangePair("fill-roughness", "fill-roughness-number", "paint.fillRoughness");
  bindCheckbox("always-on-top", "app.alwaysOnTop");
  bindRangePair("opacity", "opacity-number", "app.opacity", value => value / 100);
  bindColorPair("theme-color-picker", "theme-color", "app.themeColor");
  const qualitySelect = byId("quality-preset");
  const qualityWrap = qualitySelect.closest(".select-wrap");
  qualitySelect.addEventListener("pointerdown", () => qualityWrap?.classList.add("open"));
  qualitySelect.addEventListener("keydown", event => {
    if (["ArrowDown", "ArrowUp", "Enter", " "].includes(event.key)) {
      qualityWrap?.classList.add("open");
    }
  });
  qualitySelect.addEventListener("blur", () => qualityWrap?.classList.remove("open"));
  qualitySelect.addEventListener("change", event => {
    qualityWrap?.classList.remove("open");
    setDraftSetting("paint.qualityPreset", event.target.value);
    renderSettings(draftSnapshot);
  });

  bindCheckbox("detection-artifacts", "paint.enableDetectionArtifacts");
  bindCheckbox("local-image-enabled", "paint.useLocalImageSource");
  bindCheckbox("bypass-live-capture", "paint.bypassLiveCapture");
  byId("select-local-image").addEventListener("click", async () => {
    try {
      const result = await send("selectLocalImage");
      if (!result.success) { if (!result.cancelled) showError(result.message || "Image selection failed."); return; }
      setDraftSetting("paint.localImagePath", result.path);
      setDraftSetting("paint.useLocalImageSource", true);
      renderSettings(draftSnapshot);
      toast(`${result.name} (${result.width}×${result.height})`);
    } catch (error) { showError(error.message || String(error)); }
  });
  const detectionSelect = byId("detection-detail");
  const detectionWrap = detectionSelect.closest(".select-wrap");
  detectionSelect.addEventListener("pointerdown", () => detectionWrap?.classList.add("open"));
  detectionSelect.addEventListener("keydown", event => {
    if (["ArrowDown", "ArrowUp", "Enter", " "].includes(event.key)) {
      detectionWrap?.classList.add("open");
    }
  });
  detectionSelect.addEventListener("blur", () => detectionWrap?.classList.remove("open"));
  detectionSelect.addEventListener("change", event => {
    detectionWrap?.classList.remove("open");
    setDraftSetting("paint.detectionDetail", Number(event.target.value));
    renderSettings(draftSnapshot);
  });

  const languageSelect = byId("language");
  const languageWrap = languageSelect.closest(".select-wrap");
  languageSelect.addEventListener("pointerdown", () => languageWrap?.classList.add("open"));
  languageSelect.addEventListener("keydown", event => {
    if (["ArrowDown", "ArrowUp", "Enter", " "].includes(event.key)) {
      languageWrap?.classList.add("open");
    }
  });
  languageSelect.addEventListener("blur", () => languageWrap?.classList.remove("open"));
  languageSelect.addEventListener("change", event => {
    languageWrap?.classList.remove("open");
    setDraftSetting("app.language", event.target.value);
    render();
  });
  byId("edit-settings").addEventListener("click", beginEdit);
  byId("cancel-edit").addEventListener("click", cancelEdit);
  byId("reset-settings").addEventListener("click", resetDraft);
  byId("save-settings").addEventListener("click", () => saveDraft().catch(error => showError(error.message || String(error))));
  byId("open-logs").addEventListener("click", () => send("openLogs").catch(error => showError(error.message || String(error))));
  byId("copy-logs").addEventListener("click", async () => {
    try {
      await send("copyLogs");
      toast(i18n("toast.log.copied"));
    } catch (error) {
      showError(error.message || String(error));
    }
  });
  for (const button of document.querySelectorAll(".record-hotkey")) {
    button.addEventListener("click", () => beginHotkeyRecord(button.dataset.hotkeyKey, button.dataset.hotkeyInput));
  }
  for (const button of document.querySelectorAll(".tab")) {
    button.addEventListener("click", () => {
      activeLogFilter = button.dataset.logFilter;
      for (const tab of document.querySelectorAll(".tab")) {
        tab.classList.toggle("active", tab === button);
      }
      renderLogs(liveSnapshot?.runtime || { logs: "" });
    });
  }
  document.addEventListener("keydown", recordHotkeyFromEvent);
  refresh().catch(error => showError(error.message || String(error)));
});
