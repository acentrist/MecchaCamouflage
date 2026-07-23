function reportUiStartupFailure(kind, value) {
  try {
    const message = value instanceof Error ? value.message : String(value ?? "unknown JavaScript error");
    window.chrome?.webview?.postMessage({
      type: "uiStartupFailure",
      kind,
      message: message.slice(0, 2000)
    });
  } catch {
    // A broken WebView bridge must not turn error reporting into another page error.
  }
}

window.addEventListener("error", event => {
  const location = event.filename ? ` (${event.filename}:${event.lineno}:${event.colno})` : "";
  reportUiStartupFailure("error", `${event.message || "JavaScript error"}${location}`);
});

window.addEventListener("unhandledrejection", event => {
  reportUiStartupFailure("unhandledrejection", event.reason);
});

const pending = new Map();
const hotkeyKeys = [
  "app.startHotkey",
  "app.previewHotkey",
  "app.unpreviewHotkey",
  "app.stopHotkey",
  "app.imageStartHotkey",
  "app.imagePreviewHotkey",
  "app.imageUnpreviewHotkey",
  "app.imageStopHotkey"
];

let liveSnapshot = null;
let draftSnapshot = null;
let editing = false;
let activeLogFilter = "all";
let recordingHotkey = null;
let lastRenderedLogValue = null;
const hostedWebView = window.chrome?.webview || null;
const webview = hostedWebView || {
  addEventListener: () => {},
  postMessage: () => {}
};
const IMAGE_CANVAS_WIDTH = 1024;
const IMAGE_CANVAS_HEIGHT = 512;
const IMAGE_SOURCE_MAXIMUM_BYTES = 12 * 1024 * 1024;
const IMAGE_TOTAL_SOURCE_MAXIMUM_BYTES = 64 * 1024 * 1024;
const IMAGE_ALPHA_THRESHOLD = 128;
const IMAGE_TRANSFER_CHUNK_CHARACTERS = 128 * 1024;
const IMAGE_RESIZE_HANDLE_SIZE = 20;
const IMAGE_GUIDE_PROFILE_FILES = Object.freeze({
  round: "mesh-profiles/paintman.image-profile-v2.json",
  cube: "mesh-profiles/paintman_cube.image-profile-v2.json"
});
const imageGuideCanvasCache = new Map();
const imageGuideProfileLoads = new Map();
let imageGuideCanvasLocale = "";
let activeSettingsTab = "paint";
let imageEditor = null;
let imageCropEditor = null;

webview.addEventListener("message", event => {
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
    const previousPaintRunning = Boolean(liveSnapshot?.runtime?.paintRunning);
    liveSnapshot = message.data;
    const paintStillRunning = previousPaintRunning && Boolean(liveSnapshot.runtime?.paintRunning);
    render({ runtimeOnly: editing || paintStillRunning });
    return;
  }
  if (message.type === "event" && message.name === "toast") {
    toast(message.data.message, message.data.level || "success");
    return;
  }
  if (message.type === "event" && message.name === "zoomChanged") {
    renderFooterZoom(message.data?.percent);
  }
});

function send(command, payload = {}, timeoutMilliseconds = 0) {
  const id = crypto.randomUUID();
  let timeout = null;
  const promise = new Promise((resolve, reject) => {
    pending.set(id, {
      resolve: value => {
        if (timeout !== null) clearTimeout(timeout);
        resolve(value);
      },
      reject: value => {
        if (timeout !== null) clearTimeout(timeout);
        reject(value);
      }
    });
    if (timeoutMilliseconds > 0) {
      timeout = setTimeout(() => {
        if (!pending.has(id)) return;
        pending.delete(id);
        reject(new Error(`${command} did not respond within ${Math.round(timeoutMilliseconds / 1000)} seconds.`));
      }, timeoutMilliseconds);
    }
  });
  webview.postMessage({ id, command, payload });
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
  const locale = activeLocale();
  document.documentElement.lang = locale;
  if (imageGuideCanvasLocale !== locale) {
    imageGuideCanvasLocale = locale;
    imageGuideCanvasCache.clear();
    if (imageEditor?.guideRequested) {
      imageEditor.guideCanvas = null;
      loadImageGuideProfile(imageEditor.bodyType).catch(error => showError(error.message || String(error)));
    }
  }
  for (const element of document.querySelectorAll("[data-i18n]")) {
    element.textContent = i18n(element.dataset.i18n);
  }
  for (const element of document.querySelectorAll("[data-i18n-aria-label]")) {
    element.setAttribute("aria-label", i18n(element.dataset.i18nAriaLabel));
  }
  for (const element of document.querySelectorAll("[data-i18n-title]")) {
    element.setAttribute("title", i18n(element.dataset.i18nTitle));
  }
  for (const element of document.querySelectorAll("[data-i18n-alt]")) {
    element.setAttribute("alt", i18n(element.dataset.i18nAlt));
  }
  document.title = i18n("app.title");
}

function currentSnapshot() {
  return editing && draftSnapshot ? draftSnapshot : liveSnapshot;
}

function render({ runtimeOnly = false } = {}) {
  if (!liveSnapshot) {
    return;
  }
  if (!runtimeOnly) applyI18n();
  renderRuntime(liveSnapshot);
  if (runtimeOnly) return;
  renderSettings(currentSnapshot());
  renderImageEditor();
  applyI18n();
  renderEditState();
}

function renderFooterZoom(percent) {
  const parsed = Number(percent);
  text("footer-zoom", `${Number.isFinite(parsed) && parsed > 0 ? Math.round(parsed) : 100}%`);
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
  const passStage = runtime.paintProgressSource === "native_queue_backpressure"
    ? "painting"
    : runtime.paintProgressSource === "submission"
      ? "queueing"
      : "";
  const pass = [passStage, runtime.paintPass, runtime.paintPassProgress]
    .filter(value => value && value !== "-")
    .join(" ");
  const detail = [
    `pass ${pass || "-"}`,
    `pass ETA ${runtime.paintPassEta || "-"}`,
    `total ETA ${runtime.paintEta || "-"}`,
    `elapsed ${runtime.paintElapsed || "-"}`
  ].join(" | ");
  return `${logPrefix("INFO")} Paint: overall ${percent}% ${progressBar(percent)} | ${detail}`;
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
  const editable = canStartLiveDraftEdit();
  setNumberPair("brush-size", "brush-size-number", paint.brushSizeTexels);
  setNumberPair("color-compression-tolerance", "color-compression-tolerance-number", paint.colorCompressionTolerance);
  setChecked("auto-material", paint.autoMaterial);
  setNumberPair("metallic", "metallic-number", paint.metallic);
  setNumberPair("roughness", "roughness-number", paint.roughness);
  setNumberPair("emissive", "emissive-number", paint.emissive);
  renderRegionButtons('[data-region="paint.frontRegionMode"]', "paint.frontRegionMode", paint.frontRegionMode, editable);
  renderRegionButtons('[data-region="paint.sideRegionMode"]', "paint.sideRegionMode", paint.sideRegionMode, editable);
  renderRegionButtons('[data-region="paint.backRegionMode"]', "paint.backRegionMode", paint.backRegionMode, editable);
  setColor(paint.fillColor);
  setNumberPair("fill-metallic", "fill-metallic-number", paint.fillMetallic);
  setNumberPair("fill-roughness", "fill-roughness-number", paint.fillRoughness);
  setNumberPair("fill-emissive", "fill-emissive-number", paint.fillEmissive);
  const app = snapshot.settings.app;
  applyThemeColor(app.themeColor);
  setChecked("always-on-top", app.alwaysOnTop);
  setNumberPair("opacity", "opacity-number", Math.round(app.opacity * 100));
  setColorPair("theme-color-picker", "theme-color", app.themeColor);
  setValue("start-hotkey", app.startHotkey);
  setValue("preview-hotkey", app.previewHotkey);
  setValue("unpreview-hotkey", app.unPreviewHotkey);
  setValue("stop-hotkey", app.stopHotkey);
  setValue("image-start-hotkey", app.imageStartHotkey);
  setValue("image-preview-hotkey", app.imagePreviewHotkey);
  setValue("image-unpreview-hotkey", app.imageUnPreviewHotkey);
  setValue("image-stop-hotkey", app.imageStopHotkey);

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
    setControlDisabled(control, !editable);
  }
  for (const button of document.querySelectorAll(".record-hotkey")) {
    button.disabled = !editable;
  }

  const materialLocked = paint.autoMaterial || !editable;
  setDisabled(["metallic", "metallic-number", "roughness", "roughness-number", "emissive", "emissive-number"], materialLocked);

  const fillLocked = !editable || !usesFill(paint);
  byId("fill-section").classList.toggle("disabled", !usesFill(paint));
  setDisabled([
    "fill-color-picker",
    "fill-color",
    "fill-metallic",
    "fill-metallic-number",
    "fill-roughness",
    "fill-roughness-number",
    "fill-emissive",
    "fill-emissive-number"
  ], fillLocked);
}

function setNumberPair(sliderId, numberId, value) {
  setValue(sliderId, value);
  setValue(numberId, fmt(value));
}

function setColor(value) {
  setColorPair("fill-color-picker", "fill-color", value);
}

function renderImageFill(editor, editable) {
  if (!editor) return;
  setColorPair("image-fill-color-picker", "image-fill-color", editor.fillColor);
  setNumberPair("image-fill-metallic", "image-fill-metallic-number", editor.fillMetallic);
  setNumberPair("image-fill-roughness", "image-fill-roughness-number", editor.fillRoughness);
  setNumberPair("image-fill-emissive", "image-fill-emissive-number", editor.fillEmissive);
  const fillEnabled = usesImageFill(editor);
  byId("image-fill-section").classList.toggle("disabled", !fillEnabled);
  setDisabled([
    "image-fill-color-picker", "image-fill-color",
    "image-fill-metallic", "image-fill-metallic-number",
    "image-fill-roughness", "image-fill-roughness-number",
    "image-fill-emissive", "image-fill-emissive-number"
  ], !editable || !fillEnabled);
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
    setControlDisabled(byId(id), disabled);
  }
}

function isThemeVisibleReadOnlyControl(control) {
  return control instanceof HTMLInputElement &&
    (control.type === "range" || control.type === "checkbox");
}

function setControlDisabled(control, disabled) {
  const themeVisibleReadonly = disabled && isThemeVisibleReadOnlyControl(control);
  if (themeVisibleReadonly && document.activeElement === control) {
    control.blur();
  }
  control.disabled = disabled && !themeVisibleReadonly;
  control.classList.toggle("theme-visible-readonly", themeVisibleReadonly);
  if (themeVisibleReadonly) {
    control.setAttribute("aria-disabled", "true");
    control.tabIndex = -1;
  } else {
    control.removeAttribute("aria-disabled");
    control.removeAttribute("tabindex");
  }
}

function renderRegionButtons(selector, key, current, editable) {
  for (const container of document.querySelectorAll(selector)) {
    container.innerHTML = "";
    for (const mode of ["paint", "fill", "skip"]) {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = i18n(`mode.${mode}`);
      button.className = mode === current ? "active" : "";
      button.disabled = !editable;
      button.addEventListener("click", () => {
        if (!ensureLiveDraftEdit()) {
          return;
        }
        setDraftSetting(key, mode);
        renderSettings(draftSnapshot);
      });
      container.append(button);
    }
  }
}

function renderImageRegionButtons(editor, editable) {
  for (const container of document.querySelectorAll("[data-image-region]")) {
    const property = container.dataset.imageRegion;
    container.replaceChildren();
    for (const mode of ["fill", "skip"]) {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = i18n(`mode.${mode}`);
      button.className = `image-edit-control${editor[property] === mode ? " active" : ""}`;
      button.disabled = !editable;
      button.addEventListener("click", () => {
        if (!canEditImage() || !editable) return;
        editor[property] = mode;
        markImageDraftDirty();
      });
      container.append(button);
    }
  }
}

function renderEditState() {
  const restoringImageDesign = Boolean(imageEditor?.restoring);
  document.body.classList.toggle("editing", editing);
  byId("edit-settings").disabled = editing;
  byId("save-settings").disabled = !editing || restoringImageDesign;
  byId("cancel-edit").disabled = !editing;
  byId("reset-settings").disabled = !editing;
}

function usesFill(paint) {
  return paint.frontRegionMode === "fill" || paint.sideRegionMode === "fill" || paint.backRegionMode === "fill";
}

function usesImageFill(editor) {
  return Boolean(editor) && ["frontRegionMode", "rightRegionMode", "backRegionMode", "leftRegionMode"]
    .some(property => editor[property] === "fill");
}

// A running job owns an immutable payload. Its controls remain usable only as
// the draft for the next job; they never mutate the job already in flight.
function canStartLiveDraftEdit() {
  return Boolean(editing || liveSnapshot?.runtime?.paintRunning);
}

function ensureLiveDraftEdit() {
  if (editing) {
    return true;
  }
  if (!canStartLiveDraftEdit()) {
    return false;
  }
  beginEdit();
  return editing;
}

function beginEdit() {
  if (!liveSnapshot || editing) {
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
  setImageDesignDraftState(false).catch(error => showError(error.message || String(error)));
  send("setEditing", { editing: false }).catch(error => showError(error.message || String(error)));
  previewSavedWindow();
  render();
  loadCommittedImageDesign().catch(error => showError(error.message || String(error)));
}

async function resetDraft() {
  if (!editing || !liveSnapshot || !draftSnapshot) {
    return;
  }
  const currentProcessName = liveSnapshot.settings.app.processName;
  draftSnapshot.settings = clone(liveSnapshot.defaults);
  draftSnapshot.settings.app.processName = currentProcessName;
  draftSnapshot.language = liveSnapshot.language;
  if (imageEditor) {
    // Reset is strictly draft-local. The last GUI Save remains the active
    // F5-F8 state until the user chooses Save again.
    imageEditor = { ...newImageEditor(), loaded: true, dirty: true };
    applyCachedImageGuide(imageEditor);
    await setImageDesignDraftState(true);
  }
  render();
  previewDraftWindow();
}

async function saveDraft() {
  if (!editing || !liveSnapshot || !draftSnapshot) {
    return;
  }
  const changes = diffSnapshots(liveSnapshot, draftSnapshot);
  const imageDirty = Boolean(imageEditor?.dirty);
  if (changes.length === 0 && !imageDirty) {
    editing = false;
    draftSnapshot = null;
    closeHotkeyDialog();
    await send("setEditing", { editing: false });
    previewSavedWindow();
    render();
    return;
  }
  let result;
  if (imageDirty) {
    try {
      const design = buildImageDesign();
      const staged = await stageImageDesign(design);
      result = await send("commitSettingsWithImage", { changes, ...staged });
    } catch (error) {
      showError(error.message || String(error));
      return;
    }
  } else {
    result = await send("updateSettings", { changes });
  }
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
  if (imageEditor) {
    imageEditor.dirty = false;
    imageEditor.revision = Number(result.revision || imageEditor.revision);
    imageEditor.committedEnabled = imageEditor.layers.length > 0;
    await setImageDesignDraftState(false);
  }
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
}

function canEditControl(control = null) {
  if (ensureLiveDraftEdit() && control?.getAttribute("aria-disabled") !== "true" && !control?.disabled) {
    return true;
  }
  const snapshot = currentSnapshot();
  if (snapshot) {
    // Ranges and checkboxes stay paint-enabled solely so Chromium retains the
    // theme accent. Restore a keyboard/label-driven attempted change at once.
    renderSettings(snapshot);
  }
  return false;
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
  if (key === "app.imageUnpreviewHotkey") {
    return ["app", "imageUnPreviewHotkey"];
  }
  return key.split(".");
}

function diffSnapshots(before, after) {
  const keys = [
    "app.language",
    "paint.brushSizeTexels",
    "paint.colorCompressionTolerance",
    "paint.autoMaterial",
    "paint.metallic",
    "paint.roughness",
    "paint.emissive",
    "paint.frontRegionMode",
    "paint.sideRegionMode",
    "paint.backRegionMode",
    "paint.fillColor",
    "paint.fillMetallic",
    "paint.fillRoughness",
    "paint.fillEmissive",
    "app.alwaysOnTop",
    "app.opacity",
    "app.themeColor",
    "app.startHotkey",
    "app.previewHotkey",
    "app.unpreviewHotkey",
    "app.stopHotkey",
    "app.imageStartHotkey",
    "app.imagePreviewHotkey",
    "app.imageUnpreviewHotkey",
    "app.imageStopHotkey"
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

function imageFillColorPayload(color) {
  const normalized = normalizeColor(color) || "#FFFFFF";
  return {
    r: Number.parseInt(normalized.slice(1, 3), 16),
    g: Number.parseInt(normalized.slice(3, 5), 16),
    b: Number.parseInt(normalized.slice(5, 7), 16)
  };
}

function normalizeImageFillColor(value) {
  if (value && typeof value === "object") {
    const r = Number(value.r ?? value.R);
    const g = Number(value.g ?? value.G);
    const b = Number(value.b ?? value.B);
    if ([r, g, b].every(Number.isInteger) && [r, g, b].every(channel => channel >= 0 && channel <= 255)) {
      return "#" + [r, g, b].map(channel => channel.toString(16).padStart(2, "0")).join("").toUpperCase();
    }
  }
  return normalizeColor(value);
}

function bindRangePair(sliderId, numberId, key, transform = Number) {
  const slider = byId(sliderId);
  const number = byId(numberId);
  const commit = source => {
    if (!canEditControl(source)) {
      return;
    }
    const raw = Number(source.value);
    if (!Number.isFinite(raw)) {
      return;
    }
    const minimum = Number(source.min);
    const maximum = Number(source.max);
    const step = Number(source.step);
    const clamped = clamp(raw, minimum, maximum);
    const stepped = Number.isFinite(step) && step > 0
      ? minimum + Math.round((clamped - minimum) / step) * step
      : clamped;
    const normalized = clamp(stepped, minimum, maximum);
    slider.value = String(normalized);
    number.value = fmt(normalized);
    setDraftSetting(key, transform(normalized));
    if (key === "app.opacity") {
      send("previewWindow", { opacity: transform(normalized) }).catch(error => showError(error.message || String(error)));
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
  element.addEventListener("change", () => {
    if (!canEditControl(element)) {
      return;
    }
    setDraftSetting(key, transform(element.value));
  });
  element.addEventListener("keydown", event => {
    if (event.key === "Enter") {
      element.blur();
    }
  });
}

function bindCheckbox(id, key) {
  byId(id).addEventListener("change", event => {
    if (!canEditControl(event.target)) {
      return;
    }
    setDraftSetting(key, event.target.checked);
    renderSettings(draftSnapshot);
  });
}

function bindColorPair(pickerId, inputId, key) {
  const picker = byId(pickerId);
  const textInput = byId(inputId);
  picker.addEventListener("input", () => {
    if (!canEditControl(picker)) {
      return;
    }
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
    if (!canEditControl(textInput)) {
      return;
    }
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
  if (!ensureLiveDraftEdit()) {
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
  toast(i18n("error.operation.failed"), "error");
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

function makeImageLayer(slot = 0) {
  return {
    assetId: crypto.randomUUID(),
    fileName: "",
    mimeType: "",
    dataBase64: "",
    image: null,
    x: slot * 32,
    y: 0,
    width: IMAGE_CANVAS_WIDTH / 2,
    height: IMAGE_CANVAS_HEIGHT,
    cropX: 0,
    cropY: 0,
    cropWidth: 1,
    cropHeight: 1,
    wrapAtlasSeam: false,
    mirrorFrontBack: false
  };
}

function newImageEditor() {
  const display = byId("image-placement-canvas");
  const composition = document.createElement("canvas");
  composition.width = IMAGE_CANVAS_WIDTH;
  composition.height = IMAGE_CANVAS_HEIGHT;
  return {
    display,
    displayContext: display.getContext("2d"),
    composition,
    compositionContext: composition.getContext("2d", { willReadFrequently: true }),
    selected: -1,
    bodyType: "round",
    frontRegionMode: "fill",
    rightRegionMode: "fill",
    backRegionMode: "fill",
    leftRegionMode: "fill",
    fillColor: "#FFFFFF",
    fillMetallic: 1,
    fillRoughness: 0,
    fillEmissive: 0,
    brushSizeTexels: 5,
    colorCompressionTolerance: 0,
    metallic: 0,
    roughness: 1,
    emissive: 0,
    revision: 0,
    committedEnabled: false,
    dirty: false,
    restoring: false,
    loaded: false,
    pointer: null,
    guideCanvas: null,
    guideProfileState: "",
    guideRequested: false,
    guideError: "",
    guideReferenceOnly: false,
    layers: []
  };
}

function initializeSettingsTabs() {
  for (const tab of document.querySelectorAll("[data-settings-tab]")) {
    tab.addEventListener("click", () => {
      activeSettingsTab = tab.dataset.settingsTab;
      for (const candidate of document.querySelectorAll("[data-settings-tab]")) {
        candidate.classList.toggle("active", candidate === tab);
      }
      for (const panel of document.querySelectorAll("[data-settings-panel]")) {
        panel.hidden = panel.dataset.settingsPanel !== activeSettingsTab;
      }
      if (activeSettingsTab === "image" && imageEditor && !imageEditor.loaded) {
        loadCommittedImageDesign().catch(error => showError(error.message || String(error)));
      }
      renderImageEditor();
    });
  }
}

function initializeImageEditor() {
  imageEditor = newImageEditor();
  const input = byId("image-file-input");
  byId("image-guide-round").addEventListener("click", () => setImageBodyType("round"));
  byId("image-guide-cube").addEventListener("click", () => setImageBodyType("cube"));
  byId("image-upload").addEventListener("click", () => {
    if (canEditImage()) input.click();
  });
  byId("image-preset-load").addEventListener("click", () => loadImagePreset().catch(error => showError(error.message || String(error))));
  byId("image-preset-save").addEventListener("click", () => saveImagePreset().catch(error => showError(error.message || String(error))));
  input.addEventListener("change", event => {
    const files = Array.from(event.target.files || []);
    event.target.value = "";
    if (files.length > 0) loadImageLayers(files).catch(error => showError(error.message || String(error)));
  });
  bindImageRangePair("image-brush-size", "image-brush-size-number", "brushSizeTexels");
  bindImageRangePair("image-color-compression-tolerance", "image-color-compression-tolerance-number", "colorCompressionTolerance");
  bindImageRangePair("image-metallic", "image-metallic-number", "metallic");
  bindImageRangePair("image-roughness", "image-roughness-number", "roughness");
  bindImageRangePair("image-emissive", "image-emissive-number", "emissive");
  bindImageColorPair("image-fill-color-picker", "image-fill-color", "fillColor");
  bindImageRangePair("image-fill-metallic", "image-fill-metallic-number", "fillMetallic");
  bindImageRangePair("image-fill-roughness", "image-fill-roughness-number", "fillRoughness");
  bindImageRangePair("image-fill-emissive", "image-fill-emissive-number", "fillEmissive");
  for (const eventName of ["dragenter", "dragover"]) {
    byId("image-drop-zone").addEventListener(eventName, event => {
      if (!canEditImage()) return;
      event.preventDefault();
      byId("image-drop-zone").classList.add("dragging");
    });
  }
  byId("image-drop-zone").addEventListener("dragleave", () => byId("image-drop-zone").classList.remove("dragging"));
  byId("image-drop-zone").addEventListener("drop", event => {
    if (!canEditImage()) return;
    event.preventDefault();
    byId("image-drop-zone").classList.remove("dragging");
    const files = Array.from(event.dataTransfer?.files || []);
    if (files.length > 0) loadImageLayers(files).catch(error => showError(error.message || String(error)));
  });
  imageEditor.display.addEventListener("pointerdown", beginImagePointer);
  imageEditor.display.addEventListener("pointermove", moveImagePointer);
  imageEditor.display.addEventListener("pointerup", endImagePointer);
  imageEditor.display.addEventListener("pointercancel", endImagePointer);
  initializeImageCropEditor();
  renderImageEditor();
  loadImageGuideProfile(imageEditor.bodyType).catch(error => showError(error.message || String(error)));
}

function bindImageRangePair(sliderId, numberId, property) {
  const slider = byId(sliderId);
  const number = byId(numberId);
  const commit = source => {
    if (!canEditImage()) return;
    const raw = Number(source.value);
    const minimum = Number(slider.min);
    const maximum = Number(slider.max);
    const step = Number(slider.step);
    if (!Number.isFinite(raw)) return;
    const stepped = minimum + Math.round((clamp(raw, minimum, maximum) - minimum) / step) * step;
    imageEditor[property] = clamp(stepped, minimum, maximum);
    markImageDraftDirty();
  };
  slider.addEventListener("input", () => commit(slider));
  number.addEventListener("change", () => commit(number));
}

function bindImageColorPair(pickerId, inputId, property) {
  const picker = byId(pickerId);
  const textInput = byId(inputId);
  const commit = value => {
    if (!canEditImage()) return;
    const color = normalizeColor(value);
    if (!color) return;
    imageEditor[property] = color;
    picker.value = color;
    textInput.value = color;
    markImageDraftDirty();
  };
  picker.addEventListener("input", () => commit(picker.value));
  textInput.addEventListener("change", () => commit(textInput.value));
  textInput.addEventListener("keydown", event => {
    if (event.key === "Enter") textInput.blur();
  });
}

function canEditImage() {
  return Boolean(ensureLiveDraftEdit() && imageEditor && !imageEditor.restoring);
}

function setImageBodyType(value) {
  if (!canEditImage()) return;
  imageEditor.bodyType = value;
  imageEditor.guideCanvas = null;
  imageEditor.guideProfileState = "loading reference profile";
  imageEditor.guideError = "";
  imageEditor.guideReferenceOnly = true;
  markImageDraftDirty();
  loadImageGuideProfile(value).catch(error => showError(error.message || String(error)));
}

async function loadImageLayers(files) {
  if (!canEditImage()) return;
  let total = imageEditor.layers.reduce((sum, layer) => sum + base64ByteLength(layer.dataBase64), 0);
  for (const file of files) {
    if (!file || !["image/png", "image/jpeg"].includes(String(file.type).toLowerCase()))
      throw new Error("Image Paint accepts PNG or JPEG files only.");
    if (file.size < 1 || file.size > IMAGE_SOURCE_MAXIMUM_BYTES)
      throw new Error("Each image file must be at most 12 MiB.");
    total += file.size;
    if (total > IMAGE_TOTAL_SOURCE_MAXIMUM_BYTES)
      throw new Error("All image layers together must be at most 64 MiB.");
    const dataUrl = await readFileAsDataUrl(file);
    const image = await loadImageSource(dataUrl);
    const [, mimeType, dataBase64] = /^data:(image\/(?:png|jpeg));base64,(.+)$/i.exec(dataUrl) || [];
    if (!mimeType || !dataBase64) throw new Error("Image data could not be prepared.");
    const layer = makeImageLayer(imageEditor.layers.length);
    layer.fileName = file.name || `image-${imageEditor.layers.length + 1}`;
    layer.mimeType = mimeType.toLowerCase();
    layer.dataBase64 = dataBase64;
    layer.image = image;
    fitImageLayerToCanvas(layer);
    imageEditor.layers.push(layer);
    imageEditor.selected = imageEditor.layers.length - 1;
  }
  markImageDraftDirty();
}

function readFileAsDataUrl(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onerror = () => reject(new Error("Could not read the image file."));
    reader.onload = () => resolve(reader.result);
    reader.readAsDataURL(file);
  });
}

function loadImageSource(dataUrl) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = () => reject(new Error("Could not decode the image file."));
    image.src = dataUrl;
  });
}

function fitImageLayerToCanvas(layer) {
  if (!layer?.image) return;
  const scale = Math.min(IMAGE_CANVAS_WIDTH / layer.image.naturalWidth, IMAGE_CANVAS_HEIGHT / layer.image.naturalHeight);
  layer.width = Math.max(24, layer.image.naturalWidth * scale);
  layer.height = Math.max(24, layer.image.naturalHeight * scale);
  layer.x = (IMAGE_CANVAS_WIDTH - layer.width) / 2;
  layer.y = (IMAGE_CANVAS_HEIGHT - layer.height) / 2;
}

function fitImageLayer(index) {
  if (!canEditImage()) return;
  const layer = imageEditor.layers[index];
  if (!layer) return;
  fitImageLayerToCanvas(layer);
  imageEditor.selected = index;
  markImageDraftDirty();
}

function setImageDesignDraftState(dirty) {
  return send("setImageDesignDraftState", { dirty: Boolean(dirty) });
}

function markImageDraftDirty(renderEditor = true) {
  if (!imageEditor.dirty) {
    imageEditor.dirty = true;
    setImageDesignDraftState(true).catch(error => showError(error.message || String(error)));
  }
  if (renderEditor) renderImageEditor();
}

function imageCanvasPoint(event) {
  const bounds = imageEditor.display.getBoundingClientRect();
  return {
    x: (event.clientX - bounds.left) * IMAGE_CANVAS_WIDTH / bounds.width,
    y: (event.clientY - bounds.top) * IMAGE_CANVAS_HEIGHT / bounds.height
  };
}

function beginImagePointer(event) {
  if (!canEditImage()) return;
  const point = imageCanvasPoint(event);
  // Handles intentionally win before the rectangle body. Their hit target is
  // larger than the painted square so a corner remains usable at any canvas
  // scale and even just outside the image bounds.
  for (let index = imageEditor.layers.length - 1; index >= 0; --index) {
    const layer = imageEditor.layers[index];
    const resizeHandle = layer.image ? imageResizeHandleAt(layer, point) : "";
    if (resizeHandle) {
      beginImagePointerInteraction(event, point, index, resizeHandle);
      return;
    }
  }
  for (let index = imageEditor.layers.length - 1; index >= 0; --index) {
    const layer = imageEditor.layers[index];
    if (!layer.image || point.x < layer.x || point.x > layer.x + layer.width || point.y < layer.y || point.y > layer.y + layer.height) continue;
    beginImagePointerInteraction(event, point, index, "");
    return;
  }
}

function beginImagePointerInteraction(event, point, index, resizeHandle) {
  const layer = imageEditor.layers[index];
  imageEditor.selected = index;
  imageEditor.pointer = {
    mode: resizeHandle ? "resize" : "move",
    resizeHandle,
    x: point.x, y: point.y, startX: layer.x, startY: layer.y, startWidth: layer.width, startHeight: layer.height,
  };
  imageEditor.display.setPointerCapture(event.pointerId);
  renderImageEditor();
}

function moveImagePointer(event) {
  const pointer = imageEditor?.pointer;
  if (!pointer) return;
  const point = imageCanvasPoint(event);
  const layer = imageEditor.layers[imageEditor.selected];
  if (pointer.mode === "move") {
    layer.x = pointer.startX + point.x - pointer.x;
    layer.y = pointer.startY + point.y - pointer.y;
  } else if (pointer.mode === "resize") {
    resizeImageLayerFromCorner(layer, pointer, point);
  }
  renderImageEditor();
}

function imageResizeHandleAt(layer, point) {
  const half = IMAGE_RESIZE_HANDLE_SIZE;
  const corners = [
    ["top-left", layer.x, layer.y],
    ["top-right", layer.x + layer.width, layer.y],
    ["bottom-left", layer.x, layer.y + layer.height],
    ["bottom-right", layer.x + layer.width, layer.y + layer.height]
  ];
  return corners.find(([, x, y]) => Math.abs(point.x - x) <= half && Math.abs(point.y - y) <= half)?.[0] || "";
}

function resizeImageLayerFromCorner(layer, pointer, point) {
  const minimum = 24;
  const right = pointer.startX + pointer.startWidth;
  const bottom = pointer.startY + pointer.startHeight;
  if (pointer.resizeHandle === "top-left") {
    layer.x = Math.min(point.x, right - minimum);
    layer.y = Math.min(point.y, bottom - minimum);
    layer.width = right - layer.x;
    layer.height = bottom - layer.y;
  } else if (pointer.resizeHandle === "top-right") {
    layer.x = pointer.startX;
    layer.y = Math.min(point.y, bottom - minimum);
    layer.width = Math.max(minimum, point.x - pointer.startX);
    layer.height = bottom - layer.y;
  } else if (pointer.resizeHandle === "bottom-left") {
    layer.x = Math.min(point.x, right - minimum);
    layer.y = pointer.startY;
    layer.width = right - layer.x;
    layer.height = Math.max(minimum, point.y - pointer.startY);
  } else {
    layer.x = pointer.startX;
    layer.y = pointer.startY;
    layer.width = Math.max(minimum, point.x - pointer.startX);
    layer.height = Math.max(minimum, point.y - pointer.startY);
  }
}

function endImagePointer(event) {
  if (!imageEditor?.pointer) return;
  imageEditor.pointer = null;
  if (imageEditor.display.hasPointerCapture(event.pointerId)) imageEditor.display.releasePointerCapture(event.pointerId);
  markImageDraftDirty();
}

function drawImageInRectangle(context, image, rectangle, layer, flip = false) {
  context.save();
  context.beginPath();
  context.rect(rectangle.x, rectangle.y, rectangle.width, rectangle.height);
  context.clip();
  const crop = { x: layer.cropX, y: layer.cropY, width: layer.cropWidth, height: layer.cropHeight };
  const sourceWidth = Math.max(1, image.naturalWidth * crop.width);
  const sourceHeight = Math.max(1, image.naturalHeight * crop.height);
  const scale = Math.max(rectangle.width / sourceWidth, rectangle.height / sourceHeight);
  const width = sourceWidth * scale;
  const height = sourceHeight * scale;
  const x = rectangle.x + (rectangle.width - width) / 2;
  const y = rectangle.y + (rectangle.height - height) / 2;
  if (flip) {
    context.translate(rectangle.x * 2 + rectangle.width, 0);
    context.scale(-1, 1);
  }
  context.drawImage(
    image,
    image.naturalWidth * crop.x,
    image.naturalHeight * crop.y,
    sourceWidth,
    sourceHeight,
    x,
    y,
    width,
    height);
  context.restore();
}

function drawImageComposition() {
  const context = imageEditor.compositionContext;
  context.clearRect(0, 0, IMAGE_CANVAS_WIDTH, IMAGE_CANVAS_HEIGHT);
  for (const layer of imageEditor.layers) {
    if (!layer.image) continue;
    const rectangles = [{ x: layer.x, y: layer.y, width: layer.width, height: layer.height, flip: false }];
    if (layer.wrapAtlasSeam) {
      rectangles.push({ x: layer.x - IMAGE_CANVAS_WIDTH, y: layer.y, width: layer.width, height: layer.height, flip: false });
      rectangles.push({ x: layer.x + IMAGE_CANVAS_WIDTH, y: layer.y, width: layer.width, height: layer.height, flip: false });
    }
    if (layer.mirrorFrontBack) {
      rectangles.push({ x: (layer.x + IMAGE_CANVAS_WIDTH / 2) % IMAGE_CANVAS_WIDTH, y: layer.y, width: layer.width, height: layer.height, flip: true });
    }
    for (const rectangle of rectangles) drawImageInRectangle(context, layer.image, rectangle, layer, rectangle.flip);
  }
}

function normalizeImageGuideBodyType(bodyType) {
  return bodyType === "cube" ? "cube" : "round";
}

function loadReferenceImageGuideProfile(bodyType) {
  const normalized = normalizeImageGuideBodyType(bodyType);
  const existing = imageGuideProfileLoads.get(normalized);
  if (existing) return existing;
  const profilePath = IMAGE_GUIDE_PROFILE_FILES[normalized];
  const load = fetch(profilePath)
    .then(response => {
      if (!response.ok) throw new Error(`Reference mesh profile could not be loaded (${response.status}).`);
      return response.json();
    })
    .catch(error => {
      imageGuideProfileLoads.delete(normalized);
      throw error;
    });
  imageGuideProfileLoads.set(normalized, load);
  return load;
}

async function loadImageGuideProfile(bodyType) {
  const normalized = normalizeImageGuideBodyType(bodyType);
  if (!imageEditor || imageEditor.bodyType !== normalized) return;
  const requestedGuideLocale = activeLocale();
  const cached = imageGuideCanvasCache.get(normalized);
  imageEditor.guideCanvas = cached?.canvas || null;
  imageEditor.guideRequested = true;
  imageEditor.guideProfileState = cached?.profileState || "loading reference profile";
  imageEditor.guideError = "";
  imageEditor.guideReferenceOnly = true;
  renderImageEditor();
  try {
    const profile = await loadReferenceImageGuideProfile(normalized);
    const canvas = cached?.canvas || buildReferenceImageGuideCanvas(profile, normalized);
    const profileState = profile?.ProfileId || `${normalized} reference profile`;
    if (!imageEditor || imageEditor.bodyType !== normalized || activeLocale() !== requestedGuideLocale) return;
    imageGuideCanvasCache.set(normalized, { canvas, profileState });
    imageEditor.guideCanvas = canvas;
    imageEditor.guideProfileState = profileState;
    imageEditor.guideError = "";
  } catch (error) {
    if (!imageEditor || imageEditor.bodyType !== normalized) return;
    imageEditor.guideCanvas = cached?.canvas || null;
    imageEditor.guideProfileState = cached ? cached.profileState : "reference profile unavailable";
    imageEditor.guideError = cached ? "" : (error?.message || String(error));
  }
  renderImageEditor();
}

function applyCachedImageGuide(editor) {
  const cached = imageGuideCanvasCache.get(editor.bodyType);
  if (!cached) return;
  editor.guideCanvas = cached.canvas;
  editor.guideRequested = true;
  editor.guideProfileState = cached.profileState;
  editor.guideError = "";
}

function finiteGuideNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function referenceGuidePosition(vertex) {
  const x = finiteGuideNumber(vertex?.X);
  const y = finiteGuideNumber(vertex?.Y);
  const z = finiteGuideNumber(vertex?.Z);
  return x === null || y === null || z === null ? null : { x, y, z };
}

function referenceGuideVectorAdd(left, right) {
  return { x: left.x + right.x, y: left.y + right.y, z: left.z + right.z };
}

function referenceGuideVectorSubtract(left, right) {
  return { x: left.x - right.x, y: left.y - right.y, z: left.z - right.z };
}

function referenceGuideVectorScale(value, scale) {
  return { x: value.x * scale, y: value.y * scale, z: value.z * scale };
}

function referenceGuideVectorMultiply(left, right) {
  return { x: left.x * right.x, y: left.y * right.y, z: left.z * right.z };
}

function referenceGuideQuaternionNormalize(quaternion) {
  const length = Math.hypot(quaternion.x, quaternion.y, quaternion.z, quaternion.w);
  if (!Number.isFinite(length) || length <= 0.000001) return { x: 0, y: 0, z: 0, w: 1 };
  return { x: quaternion.x / length, y: quaternion.y / length, z: quaternion.z / length, w: quaternion.w / length };
}

function referenceGuideQuaternionMultiply(left, right) {
  return {
    x: left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
    y: left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
    z: left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
    w: left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z
  };
}

function referenceGuideQuaternionRotate(quaternion, value) {
  const q = referenceGuideQuaternionNormalize(quaternion);
  const vector = { x: q.x, y: q.y, z: q.z };
  const dot = vector.x * value.x + vector.y * value.y + vector.z * value.z;
  const lengthSquared = vector.x * vector.x + vector.y * vector.y + vector.z * vector.z;
  const cross = {
    x: vector.y * value.z - vector.z * value.y,
    y: vector.z * value.x - vector.x * value.z,
    z: vector.x * value.y - vector.y * value.x
  };
  return {
    x: 2 * vector.x * dot + value.x * (q.w * q.w - lengthSquared) + 2 * q.w * cross.x,
    y: 2 * vector.y * dot + value.y * (q.w * q.w - lengthSquared) + 2 * q.w * cross.y,
    z: 2 * vector.z * dot + value.z * (q.w * q.w - lengthSquared) + 2 * q.w * cross.z
  };
}

function referenceGuideTransformPoint(transform, point) {
  return referenceGuideVectorAdd(referenceGuideQuaternionRotate(transform.rotation, referenceGuideVectorMultiply(point, transform.scale)), transform.translation);
}

function referenceGuideInverseTransformPoint(transform, point) {
  const translated = referenceGuideVectorSubtract(point, transform.translation);
  const unrotated = referenceGuideQuaternionRotate({
    x: -transform.rotation.x,
    y: -transform.rotation.y,
    z: -transform.rotation.z,
    w: transform.rotation.w
  }, translated);
  return {
    x: Math.abs(transform.scale.x) <= 0.000001 ? unrotated.x : unrotated.x / transform.scale.x,
    y: Math.abs(transform.scale.y) <= 0.000001 ? unrotated.y : unrotated.y / transform.scale.y,
    z: Math.abs(transform.scale.z) <= 0.000001 ? unrotated.z : unrotated.z / transform.scale.z
  };
}

function referenceGuideComposeTransform(parent, child) {
  return {
    rotation: referenceGuideQuaternionNormalize(referenceGuideQuaternionMultiply(parent.rotation, child.rotation)),
    translation: referenceGuideTransformPoint(parent, child.translation),
    scale: referenceGuideVectorMultiply(parent.scale, child.scale)
  };
}

function referenceGuideBoneTransform(bone) {
  const values = [bone?.X, bone?.Y, bone?.Z, bone?.RotationX, bone?.RotationY, bone?.RotationZ, bone?.RotationW].map(finiteGuideNumber);
  if (values.some(value => value === null)) return null;
  return {
    rotation: referenceGuideQuaternionNormalize({ x: values[3], y: values[4], z: values[5], w: values[6] }),
    translation: { x: values[0], y: values[1], z: values[2] },
    scale: { x: 1, y: 1, z: 1 }
  };
}

function referenceGuideComponentTransforms(profile) {
  const bones = profile?.Bones;
  if (!Array.isArray(bones) || bones.length === 0) throw new Error("Reference mesh profile has no bind skeleton.");
  const transforms = Array(bones.length);
  const parents = Array(bones.length).fill(-1);
  for (const bone of bones) {
    const index = Number(bone?.Index);
    const parent = Number(bone?.ParentIndex);
    if (!Number.isInteger(index) || index < 0 || index >= bones.length || !Number.isInteger(parent) || parent >= bones.length) {
      throw new Error("Reference mesh profile has an invalid bind skeleton.");
    }
    parents[index] = parent;
  }
  for (let pass = 0; pass < bones.length; ++pass) {
    let progressed = false;
    for (const bone of bones) {
      const index = Number(bone.Index);
      if (transforms[index]) continue;
      const local = referenceGuideBoneTransform(bone);
      if (!local) throw new Error("Reference mesh profile has an invalid bind transform.");
      const parent = parents[index];
      if (parent < 0) {
        transforms[index] = local;
        progressed = true;
      } else if (transforms[parent]) {
        transforms[index] = referenceGuideComposeTransform(transforms[parent], local);
        progressed = true;
      }
    }
    if (!progressed) break;
  }
  if (transforms.some(transform => !transform)) throw new Error("Reference mesh profile has an unresolved bind skeleton.");
  return { bones, parents, transforms };
}

function referencePoseComponentTransforms(profile, boneCount, bodyName) {
  const source = profile?.ImageReferencePose?.ComponentTransforms;
  if (!Array.isArray(source) || source.length !== boneCount) {
    throw new Error(bodyName + " reference mesh profile has no fixed ImageReferencePose.");
  }
  const transforms = Array(boneCount);
  for (const sourceTransform of source) {
    const index = Number(sourceTransform?.Index);
    const values = [
      sourceTransform?.X, sourceTransform?.Y, sourceTransform?.Z,
      sourceTransform?.RotationX, sourceTransform?.RotationY, sourceTransform?.RotationZ, sourceTransform?.RotationW,
      sourceTransform?.ScaleX, sourceTransform?.ScaleY, sourceTransform?.ScaleZ
    ].map(finiteGuideNumber);
    if (!Number.isInteger(index) || index < 0 || index >= boneCount || values.some(value => value === null) || transforms[index]) {
      throw new Error(bodyName + " reference mesh profile has malformed ImageReferencePose data.");
    }
    transforms[index] = {
      rotation: referenceGuideQuaternionNormalize({ x: values[3], y: values[4], z: values[5], w: values[6] }),
      translation: { x: values[0], y: values[1], z: values[2] },
      scale: { x: values[7], y: values[8], z: values[9] }
    };
  }
  if (transforms.some(transform => !transform)) throw new Error(bodyName + " reference mesh profile has an incomplete ImageReferencePose.");
  return transforms;
}

function cubeReferenceComponentTransforms(profile, boneCount) {
  return referencePoseComponentTransforms(profile, boneCount, "Cube");
}

function roundReferenceComponentTransforms(profile, boneCount) {
  return referencePoseComponentTransforms(profile, boneCount, "Round");
}

function referencePoseVertices(profile, vertexCount, bodyName) {
  const source = profile?.ImageReferencePose?.Vertices;
  if (!Array.isArray(source) || source.length !== vertexCount) {
    throw new Error(bodyName + " reference mesh profile has no fixed reference vertices.");
  }
  const vertices = Array(vertexCount);
  for (const sourceVertex of source) {
    const index = Number(sourceVertex?.Index);
    const values = [sourceVertex?.X, sourceVertex?.Y, sourceVertex?.Z].map(finiteGuideNumber);
    if (!Number.isInteger(index) || index < 0 || index >= vertexCount || values.some(value => value === null) || vertices[index]) {
      throw new Error(bodyName + " reference mesh profile has malformed fixed reference vertices.");
    }
    vertices[index] = { x: values[0], y: values[1], z: values[2] };
  }
  if (vertices.some(vertex => !vertex)) throw new Error(bodyName + " reference mesh profile has incomplete fixed reference vertices.");
  return vertices;
}

function cubeReferenceVertices(profile, vertexCount) {
  return referencePoseVertices(profile, vertexCount, "Cube");
}

function roundReferenceVertices(profile, vertexCount) {
  return referencePoseVertices(profile, vertexCount, "Round");
}

function cubeCanonicalNaturalStandPositions(profile) {
  const lod = profile?.Lod0;
  if (!Array.isArray(lod?.Vertices) || lod.Vertices.length === 0) throw new Error("Cube reference mesh profile has no LOD0 vertices.");
  const bones = profile?.Bones;
  if (!Array.isArray(bones) || bones.length === 0) throw new Error("Cube reference mesh profile has no skeleton.");
  // This is the exact development capture baked into the cube profile. The
  // editor neither chooses an arm angle nor reads a live game pose. Mesh
  // vertices are captured directly so component-scale conventions cannot
  // distort the fixed shape during a later re-skin.
  const naturalTransforms = cubeReferenceComponentTransforms(profile, bones.length);
  const positions = cubeReferenceVertices(profile, lod.Vertices.length);
  const bounds = referenceGuideBounds(positions);
  const horizontalSpan = Math.max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY);
  const verticalSpan = bounds.maxZ - bounds.minZ;
  if (!Number.isFinite(horizontalSpan) || !Number.isFinite(verticalSpan) || horizontalSpan <= 0.000001 || verticalSpan <= 0.000001) {
    throw new Error("Cube reference pose has invalid projection bounds.");
  }
  return {
    positions,
    projection: {
      centerX: (bounds.minX + bounds.maxX) / 2,
      centerY: (bounds.minY + bounds.maxY) / 2,
      centerZ: (bounds.minZ + bounds.maxZ) / 2,
      pixelsPerUnit: Math.min((IMAGE_CANVAS_WIDTH / 4 - 32) / horizontalSpan, (IMAGE_CANVAS_HEIGHT - 64) / verticalSpan)
    },
    skeleton: { bones, transforms: naturalTransforms }
  };
}

function roundCanonicalNaturalStandPositions(profile) {
  const lod = profile?.Lod0;
  if (!Array.isArray(lod?.Vertices) || lod.Vertices.length === 0) throw new Error("Round reference mesh profile has no LOD0 vertices.");
  const bones = profile?.Bones;
  if (!Array.isArray(bones) || bones.length === 0) throw new Error("Round reference mesh profile has no skeleton.");
  // This is the exact development capture baked into the round profile.
  // The editor never substitutes bind-pose vertices or a live game pose.
  const naturalTransforms = roundReferenceComponentTransforms(profile, bones.length);
  const positions = roundReferenceVertices(profile, lod.Vertices.length);
  const bounds = referenceGuideBounds(positions);
  const depthIsY = bounds.maxY - bounds.minY > 0.001 && bounds.maxY - bounds.minY < bounds.maxX - bounds.minX;
  const horizontalSpan = Math.max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY);
  const verticalSpan = bounds.maxZ - bounds.minZ;
  if (!Number.isFinite(horizontalSpan) || !Number.isFinite(verticalSpan) || horizontalSpan <= 0.000001 || verticalSpan <= 0.000001) {
    throw new Error("Round reference pose has invalid projection bounds.");
  }
  return {
    positions,
    bounds,
    depthIsY,
    projection: {
      depthIsY,
      centerX: (bounds.minX + bounds.maxX) / 2,
      centerY: (bounds.minY + bounds.maxY) / 2,
      centerZ: (bounds.minZ + bounds.maxZ) / 2,
      // One scale for all four faces preserves the real 60.67cm x 24.58cm
      // round body proportions. The old per-axis normalization stretched
      // side detail by 2.47x and made its seams look jagged.
      pixelsPerUnit: Math.min((IMAGE_CANVAS_WIDTH / 4 - 32) / horizontalSpan, (IMAGE_CANVAS_HEIGHT - 64) / verticalSpan)
    },
    skeleton: { bones, transforms: naturalTransforms }
  };
}

function cubeCanonicalFace(normal) {
  return Math.abs(normal.x) >= Math.abs(normal.y)
    ? (normal.x >= 0 ? "right" : "left")
    : (normal.y >= 0 ? "back" : "front");
}

function cubeCanonicalImageCoordinate(position, face, projection) {
  const tile = { front: 0, right: 1, back: 2, left: 3 }[face];
  const horizontal = face === "front" ? position.x - projection.centerX
    : face === "back" ? projection.centerX - position.x
      : face === "right" ? position.y - projection.centerY
        : projection.centerY - position.y;
  return {
    u: (tile * (IMAGE_CANVAS_WIDTH / 4) + IMAGE_CANVAS_WIDTH / 8 + horizontal * projection.pixelsPerUnit) / IMAGE_CANVAS_WIDTH,
    v: 0.5 + (position.z - projection.centerZ) * projection.pixelsPerUnit / IMAGE_CANVAS_HEIGHT
  };
}

function drawReferenceCanonicalSkeleton(context, skeleton, coordinate) {
  if (!skeleton || !Array.isArray(skeleton.bones) || !Array.isArray(skeleton.transforms)) return;
  const transformAt = index => Number.isInteger(index) ? skeleton.transforms[index] : null;
  const faces = ["front", "right", "back", "left"];
  context.save();
  context.strokeStyle = "rgba(230, 230, 230, 0.44)";
  context.fillStyle = "rgba(245, 245, 245, 0.58)";
  context.lineWidth = 1.5;
  for (const face of faces) {
    for (const bone of skeleton.bones) {
      const index = Number(bone?.Index);
      const parent = Number(bone?.ParentIndex);
      const current = transformAt(index)?.translation;
      const parentPosition = transformAt(parent)?.translation;
      if (!current || !parentPosition) continue;
      const start = coordinate(parentPosition, face);
      const end = coordinate(current, face);
      context.beginPath();
      context.moveTo(start.u * IMAGE_CANVAS_WIDTH, (1 - start.v) * IMAGE_CANVAS_HEIGHT);
      context.lineTo(end.u * IMAGE_CANVAS_WIDTH, (1 - end.v) * IMAGE_CANVAS_HEIGHT);
      context.stroke();
    }
    for (const bone of skeleton.bones) {
      const position = transformAt(Number(bone?.Index))?.translation;
      if (!position) continue;
      const point = coordinate(position, face);
      context.beginPath();
      context.arc(point.u * IMAGE_CANVAS_WIDTH, (1 - point.v) * IMAGE_CANVAS_HEIGHT, 2.5, 0, Math.PI * 2);
      context.fill();
    }
  }
  context.restore();
}

function drawCubeCanonicalSkeleton(context, skeleton, projection) {
  drawReferenceCanonicalSkeleton(context, skeleton, (position, face) => cubeCanonicalImageCoordinate(position, face, projection));
}

function roundCanonicalImageCoordinate(position, face, projection) {
  let horizontal = projection.depthIsY ? position.x - projection.centerX : position.y - projection.centerY;
  const tile = { front: 0, right: 1, back: 2, left: 3 }[face];
  if (face === "right") {
    horizontal = projection.depthIsY ? position.y - projection.centerY : position.x - projection.centerX;
  } else if (face === "back") {
    horizontal = -horizontal;
  } else if (face === "left") {
    horizontal = projection.depthIsY ? projection.centerY - position.y : projection.centerX - position.x;
  }
  return {
    u: (tile * (IMAGE_CANVAS_WIDTH / 4) + IMAGE_CANVAS_WIDTH / 8 + horizontal * projection.pixelsPerUnit) / IMAGE_CANVAS_WIDTH,
    v: 0.5 + (position.z - projection.centerZ) * projection.pixelsPerUnit / IMAGE_CANVAS_HEIGHT
  };
}

function drawRoundCanonicalSkeleton(context, skeleton, projection) {
  drawReferenceCanonicalSkeleton(context, skeleton, (position, face) => roundCanonicalImageCoordinate(position, face, projection));
}

function buildCubeCanonicalImageGuideCanvas(profile) {
  const lod = profile?.Lod0;
  const indices = lod?.Indices;
  if (!Array.isArray(indices) || indices.length < 3 || indices.length % 3 !== 0) throw new Error("Cube reference mesh profile has no valid LOD0 geometry.");
  const { positions, projection, skeleton } = cubeCanonicalNaturalStandPositions(profile);
  const triangles = [];
  for (let index = 0; index < indices.length; index += 3) {
    const first = positions[Number(indices[index])];
    const second = positions[Number(indices[index + 1])];
    const third = positions[Number(indices[index + 2])];
    if (!first || !second || !third) continue;
    const normal = normalizeReferenceGuideVector({
      x: (second.y - first.y) * (third.z - first.z) - (second.z - first.z) * (third.y - first.y),
      y: (second.z - first.z) * (third.x - first.x) - (second.x - first.x) * (third.z - first.z),
      z: (second.x - first.x) * (third.y - first.y) - (second.y - first.y) * (third.x - first.x)
    });
    if (!normal) continue;
    const face = cubeCanonicalFace(normal);
    appendReferenceGuideTriangle(triangles, [
      cubeCanonicalImageCoordinate(first, face, projection),
      cubeCanonicalImageCoordinate(second, face, projection),
      cubeCanonicalImageCoordinate(third, face, projection)
    ]);
  }
  if (triangles.length === 0) throw new Error("Cube reference pose produced no projection guide.");
  return buildReferenceImageGuideCanvasFromTriangles(triangles, { cubeSkeleton: skeleton, projection });
}

function referenceGuideBounds(positions) {
  const bounds = { minX: Infinity, maxX: -Infinity, minY: Infinity, maxY: -Infinity, minZ: Infinity, maxZ: -Infinity };
  for (const position of positions) {
    bounds.minX = Math.min(bounds.minX, position.x); bounds.maxX = Math.max(bounds.maxX, position.x);
    bounds.minY = Math.min(bounds.minY, position.y); bounds.maxY = Math.max(bounds.maxY, position.y);
    bounds.minZ = Math.min(bounds.minZ, position.z); bounds.maxZ = Math.max(bounds.maxZ, position.z);
  }
  if (!Object.values(bounds).every(Number.isFinite)) throw new Error("Reference mesh profile has invalid vertex bounds.");
  return bounds;
}

function normalizeReferenceGuideVector(vector) {
  const length = Math.hypot(vector.x, vector.y, vector.z);
  if (!Number.isFinite(length) || length <= 0.000001) return null;
  return { x: vector.x / length, y: vector.y / length, z: vector.z / length };
}

function referenceGuideTile(value) {
  return clamp(Math.floor(clamp(value, 0, 1) * 4 - 0.0000001), 0, 3);
}

function appendReferenceGuideTriangle(output, points, edge = false) {
  if (!points.every(point => Number.isFinite(point.u) && Number.isFinite(point.v) && point.u >= -0.000001 && point.u <= 1.000001 && point.v >= -0.000001 && point.v <= 1.000001)) return;
  const minU = Math.min(...points.map(point => point.u));
  const maxU = Math.max(...points.map(point => point.u));
  if (maxU - minU > 0.2500001 || points.some(point => referenceGuideTile(point.u) !== referenceGuideTile(points[0].u))) return;
  output.push({ u0: points[0].u, v0: points[0].v, u1: points[1].u, v1: points[1].v, u2: points[2].u, v2: points[2].v, edge });
}

function buildReferenceImageGuideCanvas(profile, bodyType) {
  if (normalizeImageGuideBodyType(bodyType) === "cube") return buildCubeCanonicalImageGuideCanvas(profile);
  const lod = profile?.Lod0;
  const indices = lod?.Indices;
  if (!Array.isArray(lod?.Vertices) || !Array.isArray(indices) || indices.length < 3 || indices.length % 3 !== 0) {
    throw new Error("Reference mesh profile has no valid LOD0 geometry.");
  }
  const { positions, projection, skeleton } = roundCanonicalNaturalStandPositions(profile);
  const guideTriangles = [];
  for (let index = 0; index < indices.length; index += 3) {
    const first = positions[Number(indices[index])];
    const second = positions[Number(indices[index + 1])];
    const third = positions[Number(indices[index + 2])];
    if (!first || !second || !third) continue;
    // The canvas is a four-view reference, not a partition of material
    // triangles by normal. Project the complete fixed mesh into every view so
    // each tile is recognisably the same natural-standing body.
    for (const face of ["front", "right", "back", "left"]) {
      appendReferenceGuideTriangle(guideTriangles, [
        roundCanonicalImageCoordinate(first, face, projection),
        roundCanonicalImageCoordinate(second, face, projection),
        roundCanonicalImageCoordinate(third, face, projection)
      ]);
    }
  }
  if (guideTriangles.length === 0) throw new Error("Reference mesh profile produced no atlas guide.");
  return buildReferenceImageGuideCanvasFromTriangles(guideTriangles, { roundSkeleton: skeleton, roundProjection: projection });
}

function buildReferenceImageGuideCanvasFromTriangles(triangles, options = {}) {
  if (!Array.isArray(triangles) || triangles.length === 0) {
    throw new Error("The reference mesh guide contains no atlas triangles.");
  }
  const guide = document.createElement("canvas");
  guide.width = IMAGE_CANVAS_WIDTH;
  guide.height = IMAGE_CANVAS_HEIGHT;
  const context = guide.getContext("2d");
  const silhouette = document.createElement("canvas");
  silhouette.width = IMAGE_CANVAS_WIDTH;
  silhouette.height = IMAGE_CANVAS_HEIGHT;
  const silhouetteContext = silhouette.getContext("2d");
  const labels = [i18n("region.front"), i18n("region.right"), i18n("region.back"), i18n("region.left")];
  // Rasterise every triangle into an opaque mask first, then display that
  // mask once. Stacking translucent triangles made a checker/grid pattern
  // which looked like image pixels even though it was only a guide overlay.
  silhouetteContext.fillStyle = "#ffffff";
  for (const triangle of triangles) {
    const points = [
      [Number(triangle.u0), Number(triangle.v0)],
      [Number(triangle.u1), Number(triangle.v1)],
      [Number(triangle.u2), Number(triangle.v2)]
    ];
    if (!points.flat().every(Number.isFinite) || points.some(([, v]) => v < -0.000001 || v > 1.000001)) {
      throw new Error("The reference mesh guide contains invalid atlas coordinates.");
    }
    silhouetteContext.beginPath();
    silhouetteContext.moveTo(points[0][0] * IMAGE_CANVAS_WIDTH, (1 - points[0][1]) * IMAGE_CANVAS_HEIGHT);
    silhouetteContext.lineTo(points[1][0] * IMAGE_CANVAS_WIDTH, (1 - points[1][1]) * IMAGE_CANVAS_HEIGHT);
    silhouetteContext.lineTo(points[2][0] * IMAGE_CANVAS_WIDTH, (1 - points[2][1]) * IMAGE_CANVAS_HEIGHT);
    silhouetteContext.closePath();
    silhouetteContext.fill();
  }
  context.save();
  context.globalAlpha = 0.24;
  context.drawImage(silhouette, 0, 0);
  context.restore();
  if (options.cubeSkeleton) drawCubeCanonicalSkeleton(context, options.cubeSkeleton, options.projection);
  if (options.roundSkeleton) drawRoundCanonicalSkeleton(context, options.roundSkeleton, options.roundProjection);
  for (let face = 0; face < 4; ++face) {
    const xOffset = face * IMAGE_CANVAS_WIDTH / 4;
    context.strokeStyle = "rgba(255,255,255,0.36)";
    context.lineWidth = 1;
    context.strokeRect(xOffset + 1, 1, IMAGE_CANVAS_WIDTH / 4 - 2, IMAGE_CANVAS_HEIGHT - 2);
    context.fillStyle = "rgba(255,255,255,0.78)";
    context.font = "700 24px D-DIN, sans-serif";
    context.fillText(labels[face], xOffset + 12, 31);
  }
  return guide;
}

function drawProfileGuide(context) {
  if (imageEditor.guideCanvas) {
    context.drawImage(imageEditor.guideCanvas, 0, 0);
    return;
  }
  context.save();
  context.strokeStyle = "rgba(255,255,255,0.36)";
  context.fillStyle = "rgba(255,255,255,0.78)";
  context.font = "700 24px D-DIN, sans-serif";
  const labels = [i18n("region.front"), i18n("region.right"), i18n("region.back"), i18n("region.left")];
  for (let face = 0; face < 4; ++face) {
    const x = face * IMAGE_CANVAS_WIDTH / 4;
    context.strokeRect(x + 1, 1, IMAGE_CANVAS_WIDTH / 4 - 2, IMAGE_CANVAS_HEIGHT - 2);
    context.fillText(labels[face], x + 12, 31);
  }
  context.restore();
}

function renderImageEditor() {
  if (!imageEditor) return;
  drawImageComposition();
  const context = imageEditor.displayContext;
  context.clearRect(0, 0, IMAGE_CANVAS_WIDTH, IMAGE_CANVAS_HEIGHT);
  context.drawImage(imageEditor.composition, 0, 0);
  drawProfileGuide(context);
  for (let index = 0; index < imageEditor.layers.length; ++index) {
    const layer = imageEditor.layers[index];
    if (!layer.image) continue;
    if (editing) {
      context.strokeStyle = index === imageEditor.selected ? "#ffffff" : "#999999";
      context.lineWidth = index === imageEditor.selected ? 4 : 2;
      context.strokeRect(layer.x, layer.y, layer.width, layer.height);
      if (index === imageEditor.selected) {
        context.fillStyle = "#ffffff";
        const half = IMAGE_RESIZE_HANDLE_SIZE / 2;
        for (const [x, y] of [
          [layer.x, layer.y],
          [layer.x + layer.width, layer.y],
          [layer.x, layer.y + layer.height],
          [layer.x + layer.width, layer.y + layer.height]
        ]) {
          context.fillRect(x - half, y - half, IMAGE_RESIZE_HANDLE_SIZE, IMAGE_RESIZE_HANDLE_SIZE);
        }
      }
    }
  }
  for (const [id, active] of [["image-guide-round", imageEditor.bodyType === "round"], ["image-guide-cube", imageEditor.bodyType === "cube"]]) {
    byId(id).classList.toggle("active", active);
  }
  setNumberPair("image-brush-size", "image-brush-size-number", imageEditor.brushSizeTexels);
  setNumberPair("image-color-compression-tolerance", "image-color-compression-tolerance-number", imageEditor.colorCompressionTolerance);
  setNumberPair("image-metallic", "image-metallic-number", imageEditor.metallic);
  setNumberPair("image-roughness", "image-roughness-number", imageEditor.roughness);
  setNumberPair("image-emissive", "image-emissive-number", imageEditor.emissive);
  const editable = canStartLiveDraftEdit() && !imageEditor.restoring;
  for (const control of document.querySelectorAll(".image-edit-control")) control.disabled = !editable;
  renderImageRegionButtons(imageEditor, editable);
  renderImageFill(imageEditor, editable);
  byId("image-drop-zone").classList.toggle("readonly", !editable);
  renderImageLayerList();
}

function renderImageLayerList() {
  const list = byId("image-layer-list");
  list.replaceChildren();
  imageEditor.layers.forEach((layer, index) => {
    const row = document.createElement("div");
    row.className = "image-layer-row";
    const name = document.createElement("button");
    name.type = "button";
    name.disabled = !canStartLiveDraftEdit() || imageEditor.restoring;
    name.className = `image-layer-item${index === imageEditor.selected ? " active" : ""}`;
    name.textContent = layer.fileName || i18n("image.layer.default", index + 1);
    name.title = layer.fileName || i18n("image.layer.default", index + 1);
    name.addEventListener("click", () => {
      if (!canEditImage()) return;
      imageEditor.selected = index;
      renderImageEditor();
    });
    row.append(name);
    const tools = document.createElement("div");
    tools.className = "image-layer-tools";
    const action = (label, title, callback, active = false, danger = false) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = `image-layer-action${active ? " active" : ""}${danger ? " danger" : ""}`;
      button.textContent = label;
      button.title = title;
      button.disabled = !canStartLiveDraftEdit() || imageEditor.restoring;
      button.addEventListener("click", event => {
        event.stopPropagation();
        if (!canEditImage()) return;
        callback();
      });
      tools.append(button);
    };
    action(i18n("image.action.wrap"), i18n("image.action.wrap.title"), () => {
      layer.wrapAtlasSeam = !layer.wrapAtlasSeam;
      imageEditor.selected = index;
      markImageDraftDirty();
    }, layer.wrapAtlasSeam);
    action(i18n("image.action.mirror"), i18n("image.action.mirror.title"), () => {
      layer.mirrorFrontBack = !layer.mirrorFrontBack;
      imageEditor.selected = index;
      markImageDraftDirty();
    }, layer.mirrorFrontBack);
    action(i18n("image.action.fit"), i18n("image.action.fit.title"), () => fitImageLayer(index));
    action(i18n("image.action.crop"), i18n("image.action.crop.title"), () => openImageCropEditor(index));
    action("×", i18n("image.action.remove", layer.fileName || i18n("image.layer.default", index + 1)), () => {
      imageEditor.layers.splice(index, 1);
      imageEditor.selected = imageEditor.layers.length === 0 ? -1 : Math.min(index, imageEditor.layers.length - 1);
      markImageDraftDirty();
    }, false, true);
    row.append(tools);
    list.append(row);
  });
}

function bytesToBase64(bytes) {
  const chunk = 0x8000;
  let binary = "";
  for (let index = 0; index < bytes.length; index += chunk) binary += String.fromCharCode(...bytes.subarray(index, Math.min(index + chunk, bytes.length)));
  return btoa(binary);
}

function buildImageDesign() {
  drawImageComposition();
  const pixels = imageEditor.compositionContext.getImageData(0, 0, IMAGE_CANVAS_WIDTH, IMAGE_CANVAS_HEIGHT).data;
  for (let index = 0; index < pixels.length; index += 4) {
    if (pixels[index + 3] < IMAGE_ALPHA_THRESHOLD) {
      pixels[index] = 0; pixels[index + 1] = 0; pixels[index + 2] = 0; pixels[index + 3] = 0;
    } else {
      pixels[index + 3] = 255;
    }
  }
  const layers = imageEditor.layers.filter(layer => layer.image).map(layer => ({
    assetId: layer.assetId,
    fileName: layer.fileName,
    mimeType: layer.mimeType,
    dataBase64: layer.dataBase64,
    centerX: (layer.x + layer.width / 2) / IMAGE_CANVAS_WIDTH,
    centerY: (layer.y + layer.height / 2) / IMAGE_CANVAS_HEIGHT,
    width: layer.width / IMAGE_CANVAS_WIDTH,
    height: layer.height / IMAGE_CANVAS_HEIGHT,
    cropX: layer.cropX, cropY: layer.cropY, cropWidth: layer.cropWidth, cropHeight: layer.cropHeight,
    wrapAtlasSeam: Boolean(layer.wrapAtlasSeam),
    mirrorFrontBack: Boolean(layer.mirrorFrontBack)
  }));
  return {
    enabled: layers.length > 0,
    revision: imageEditor.revision,
    canvasEncodingVersion: 0,
    bodyType: imageEditor.bodyType,
    alphaMode: "skip",
    frontRegionMode: imageEditor.frontRegionMode,
    rightRegionMode: imageEditor.rightRegionMode,
    backRegionMode: imageEditor.backRegionMode,
    leftRegionMode: imageEditor.leftRegionMode,
    placement: "fit",
    fillColor: imageFillColorPayload(imageEditor.fillColor),
    fillMetallic: imageEditor.fillMetallic,
    fillRoughness: imageEditor.fillRoughness,
    fillEmissive: imageEditor.fillEmissive,
    brushSizeTexels: imageEditor.brushSizeTexels,
    colorCompressionTolerance: imageEditor.colorCompressionTolerance,
    metallic: imageEditor.metallic,
    roughness: imageEditor.roughness,
    emissive: imageEditor.emissive,
    canvasRgbaBase64: layers.length > 0 ? bytesToBase64(pixels) : "",
    layers
  };
}

async function stageImageDesign(design) {
  const stagedDesign = clone(design);
  let transferId = null;
  if (design.enabled) {
    transferId = crypto.randomUUID();
    await stageImageDesignAsset(transferId, "canvas", design.canvasRgbaBase64);
    for (const [index, layer] of design.layers.entries()) {
      await stageImageDesignAsset(transferId, `layer${index}`, layer.dataBase64);
      stagedDesign.layers[index].dataBase64 = "";
    }
    stagedDesign.canvasRgbaBase64 = "";
  }
  return { design: stagedDesign, ...(transferId ? { transferId } : {}) };
}

async function saveImagePreset() {
  if (!canEditImage()) return;
  const staged = await stageImageDesign(buildImageDesign());
  const result = await send("saveImagePreset", staged);
  if (!result?.success) throw new Error(result?.message || "The preset could not be saved.");
  if (!result.cancelled) toast(i18n("toast.image.preset.saved"));
}

async function stageImageDesignAsset(transferId, asset, data) {
  if (typeof data !== "string" || data.length === 0) {
    throw new Error(`The ${asset} image data is missing.`);
  }
  let index = 0;
  for (let offset = 0; offset < data.length; offset += IMAGE_TRANSFER_CHUNK_CHARACTERS) {
    const result = await send("stageImageDesignChunk", {
      transferId,
      asset,
      index,
      data: data.slice(offset, offset + IMAGE_TRANSFER_CHUNK_CHARACTERS)
    });
    if (!result?.success) throw new Error(result?.message || "The image upload failed.");
    index++;
  }
}

async function loadImageAsset(asset, transferId = null) {
  const chunks = [];
  for (let index = 0; index < 512; index++) {
    const result = await send(transferId ? "getLoadedImagePresetChunk" : "getImageAssetChunk", {
      asset,
      index,
      ...(transferId ? { transferId } : {})
    });
    if (!result?.success || typeof result.data !== "string") {
      throw new Error(result?.message || `The ${asset} image data could not be loaded.`);
    }
    chunks.push(result.data);
    if (result.complete) return chunks.join("");
  }
  throw new Error(`The ${asset} image data is too large.`);
}

async function loadCommittedImageDesign() {
  if (!hostedWebView || !imageEditor) return;
  const response = await send("getActiveImage");
  if (!response?.success || !response.design) throw new Error(response?.message || "The active Image Paint state could not be loaded.");
  await hydrateImageEditor(response.design, null, false);
}

async function loadImagePreset() {
  if (!canEditImage()) return;
  const response = await send("loadImagePreset");
  if (!response?.success) throw new Error(response?.message || "The preset could not be loaded.");
  if (response.cancelled) return;
  await hydrateImageEditor(response.design, response.transferId, true);
  toast(i18n("toast.image.preset.loaded"));
}

async function hydrateImageEditor(design, transferId, draft) {
  const next = newImageEditor();
  next.bodyType = design?.bodyType === "cube" ? "cube" : "round";
  next.frontRegionMode = design?.frontRegionMode === "skip" ? "skip" : "fill";
  next.rightRegionMode = design?.rightRegionMode === "skip" ? "skip" : "fill";
  next.backRegionMode = design?.backRegionMode === "skip" ? "skip" : "fill";
  next.leftRegionMode = design?.leftRegionMode === "skip" ? "skip" : "fill";
  next.fillColor = normalizeImageFillColor(design?.fillColor) || "#FFFFFF";
  next.fillMetallic = clamp(Number(design?.fillMetallic ?? 1), 0, 1);
  next.fillRoughness = clamp(Number(design?.fillRoughness ?? 0), 0, 1);
  next.fillEmissive = clamp(Number(design?.fillEmissive ?? 0), 0, 1);
  const legacyWrapAtlasSeam = Boolean(design?.wrapFaces);
  const legacyMirrorFrontBack = Boolean(design?.mirrorFrontBack);
  next.brushSizeTexels = clamp(Number(design?.brushSizeTexels ?? 5), 1, 10);
  next.colorCompressionTolerance = clamp(Number(design?.colorCompressionTolerance ?? 0), 0, 10);
  next.metallic = clamp(Number(design?.metallic ?? 0), 0, 1);
  next.roughness = clamp(Number(design?.roughness ?? 1), 0, 1);
  next.emissive = clamp(Number(design?.emissive ?? 0), 0, 1);
  next.revision = Number(design?.revision || 0);
  next.committedEnabled = Boolean(design?.enabled);
  for (const [index, saved] of (design?.layers || []).entries()) {
    const layer = makeImageLayer(index);
    layer.assetId = saved.assetId || crypto.randomUUID();
    layer.fileName = saved.fileName || `image-${index + 1}`;
    layer.mimeType = saved.mimeType || "image/png";
    layer.dataBase64 = await loadImageAsset(`layer${index}`, transferId);
    layer.image = await loadImageSource(`data:${layer.mimeType};base64,${layer.dataBase64}`);
    layer.width = clamp(Number(saved.width || 0.5), 0.01, 4) * IMAGE_CANVAS_WIDTH;
    layer.height = clamp(Number(saved.height || 1), 0.01, 4) * IMAGE_CANVAS_HEIGHT;
    layer.x = Number(saved.centerX || 0.5) * IMAGE_CANVAS_WIDTH - layer.width / 2;
    layer.y = Number(saved.centerY || 0.5) * IMAGE_CANVAS_HEIGHT - layer.height / 2;
    layer.cropX = Number(saved.cropX ?? 0); layer.cropY = Number(saved.cropY ?? 0);
    layer.cropWidth = Number(saved.cropWidth ?? 1); layer.cropHeight = Number(saved.cropHeight ?? 1);
    layer.wrapAtlasSeam = Boolean(saved.wrapAtlasSeam ?? legacyWrapAtlasSeam);
    layer.mirrorFrontBack = Boolean(saved.mirrorFrontBack ?? legacyMirrorFrontBack);
    next.layers.push(layer);
  }
  next.selected = next.layers.length > 0 ? 0 : -1;
  next.loaded = true;
  next.dirty = Boolean(draft);
  applyCachedImageGuide(next);
  imageEditor = next;
  await setImageDesignDraftState(Boolean(draft));
  renderImageEditor();
  loadImageGuideProfile(imageEditor.bodyType).catch(error => showError(error.message || String(error)));
}

function base64ByteLength(value) {
  const text = String(value || "");
  if (!text) return 0;
  const padding = text.endsWith("==") ? 2 : text.endsWith("=") ? 1 : 0;
  return Math.floor(text.length * 3 / 4) - padding;
}

function initializeImageCropEditor() {
  byId("crop-editor-selection").addEventListener("pointerdown", beginImageCropDrag);
  byId("crop-editor-selection").addEventListener("pointermove", moveImageCropDrag);
  byId("crop-editor-selection").addEventListener("pointerup", endImageCropDrag);
  byId("crop-editor-selection").addEventListener("pointercancel", endImageCropDrag);
  byId("crop-editor-zoom").addEventListener("input", event => updateImageCropZoom(event.target.value));
  byId("crop-editor-reset").addEventListener("click", () => {
    if (!imageCropEditor) return;
    imageCropEditor.draft = clone(imageCropEditor.base);
    byId("crop-editor-zoom").value = "100";
    renderImageCropSelection();
  });
  byId("crop-editor-cancel").addEventListener("click", closeImageCropEditor);
  byId("crop-editor-apply").addEventListener("click", applyImageCrop);
}

function defaultImageCropForLayer(layer) {
  const image = layer?.image;
  if (!image?.naturalWidth || !image?.naturalHeight || !Number.isFinite(layer.width) || !Number.isFinite(layer.height) ||
      layer.width <= 0 || layer.height <= 0) {
    return { x: 0, y: 0, width: 1, height: 1 };
  }
  const sourceAspect = image.naturalWidth / image.naturalHeight;
  const targetAspect = layer.width / layer.height;
  if (sourceAspect > targetAspect) {
    const width = targetAspect / sourceAspect;
    return { x: (1 - width) / 2, y: 0, width, height: 1 };
  }
  const height = sourceAspect / targetAspect;
  return { x: 0, y: (1 - height) / 2, width: 1, height };
}

function cropAtImageZoom(base, factor, centerX, centerY) {
  const width = base.width / factor;
  const height = base.height / factor;
  return {
    x: clamp(centerX - width / 2, 0, 1 - width),
    y: clamp(centerY - height / 2, 0, 1 - height),
    width,
    height
  };
}

function normalizeImageCropForLayer(layer, base) {
  const crop = {
    x: Number(layer.cropX ?? 0), y: Number(layer.cropY ?? 0),
    width: Number(layer.cropWidth ?? 1), height: Number(layer.cropHeight ?? 1)
  };
  const centerX = Number.isFinite(crop.x) && Number.isFinite(crop.width) ? crop.x + crop.width / 2 : 0.5;
  const centerY = Number.isFinite(crop.y) && Number.isFinite(crop.height) ? crop.y + crop.height / 2 : 0.5;
  const factor = Number.isFinite(crop.width) && Number.isFinite(crop.height) && crop.width > 0 && crop.height > 0
    ? clamp(Math.min(base.width / crop.width, base.height / crop.height), 1, 4)
    : 1;
  return cropAtImageZoom(base, factor, centerX, centerY);
}

async function openImageCropEditor(index = imageEditor?.selected ?? -1) {
  if (!canEditImage()) return;
  const layer = imageEditor.layers[index];
  if (!layer?.image) return;
  imageEditor.selected = index;
  const base = defaultImageCropForLayer(layer);
  const draft = normalizeImageCropForLayer(layer, base);
  imageCropEditor = {
    layerIndex: index,
    base,
    draft,
    drag: null
  };
  const cropImage = byId("crop-editor-image");
  cropImage.src = layer.image.src;
  byId("crop-editor-zoom").value = String(Math.round(Math.min(base.width / draft.width, base.height / draft.height) * 100));
  byId("crop-editor-dialog").hidden = false;
  try {
    await cropImage.decode();
  } catch {
    // The canvas source was already decoded when it was uploaded. Keep the
    // crop modal open even if a second HTML image decode is unavailable.
  }
  renderImageCropSelection();
}

function closeImageCropEditor() {
  imageCropEditor = null;
  byId("crop-editor-dialog").hidden = true;
}

function renderImageCropSelection() {
  if (!imageCropEditor) return;
  const crop = imageCropEditor.draft;
  const selection = byId("crop-editor-selection");
  selection.style.left = `${crop.x * 100}%`;
  selection.style.top = `${crop.y * 100}%`;
  selection.style.width = `${crop.width * 100}%`;
  selection.style.height = `${crop.height * 100}%`;
}

function beginImageCropDrag(event) {
  if (!imageCropEditor) return;
  imageCropEditor.drag = { pointerId: event.pointerId, startX: event.clientX, startY: event.clientY, crop: clone(imageCropEditor.draft) };
  event.currentTarget.setPointerCapture(event.pointerId);
}

function moveImageCropDrag(event) {
  const drag = imageCropEditor?.drag;
  if (!drag || drag.pointerId !== event.pointerId) return;
  const bounds = byId("crop-editor-stage").getBoundingClientRect();
  imageCropEditor.draft.x = clamp(drag.crop.x + (event.clientX - drag.startX) / bounds.width, 0, 1 - drag.crop.width);
  imageCropEditor.draft.y = clamp(drag.crop.y + (event.clientY - drag.startY) / bounds.height, 0, 1 - drag.crop.height);
  renderImageCropSelection();
}

function endImageCropDrag(event) {
  if (imageCropEditor?.drag?.pointerId === event.pointerId) imageCropEditor.drag = null;
}

function updateImageCropZoom(value) {
  if (!imageCropEditor) return;
  const crop = imageCropEditor.draft;
  const factor = clamp(Number(value) / 100, 1, 4);
  const centerX = crop.x + crop.width / 2;
  const centerY = crop.y + crop.height / 2;
  imageCropEditor.draft = cropAtImageZoom(imageCropEditor.base, factor, centerX, centerY);
  renderImageCropSelection();
}

function applyImageCrop() {
  if (!imageCropEditor || !canEditImage()) return;
  const layer = imageEditor.layers[imageCropEditor.layerIndex];
  if (layer) Object.assign(layer, {
    cropX: imageCropEditor.draft.x,
    cropY: imageCropEditor.draft.y,
    cropWidth: imageCropEditor.draft.width,
    cropHeight: imageCropEditor.draft.height
  });
  closeImageCropEditor();
  markImageDraftDirty();
}

document.addEventListener("DOMContentLoaded", () => {
  initializeSettingsTabs();
  initializeImageEditor();
  bindRangePair("brush-size", "brush-size-number", "paint.brushSizeTexels");
  bindRangePair("color-compression-tolerance", "color-compression-tolerance-number", "paint.colorCompressionTolerance");
  bindCheckbox("auto-material", "paint.autoMaterial");
  bindRangePair("metallic", "metallic-number", "paint.metallic");
  bindRangePair("roughness", "roughness-number", "paint.roughness");
  bindRangePair("emissive", "emissive-number", "paint.emissive");
  bindColorPair("fill-color-picker", "fill-color", "paint.fillColor");
  bindRangePair("fill-metallic", "fill-metallic-number", "paint.fillMetallic");
  bindRangePair("fill-roughness", "fill-roughness-number", "paint.fillRoughness");
  bindRangePair("fill-emissive", "fill-emissive-number", "paint.fillEmissive");
  bindCheckbox("always-on-top", "app.alwaysOnTop");
  bindRangePair("opacity", "opacity-number", "app.opacity", value => value / 100);
  bindColorPair("theme-color-picker", "theme-color", "app.themeColor");
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
    if (!canEditControl(languageSelect)) {
      return;
    }
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
  webview.postMessage({ type: "uiReady" });
  if (hostedWebView) {
    refresh().catch(error => showError(error.message || String(error)));
  } else {
    document.documentElement.dataset.previewMode = "true";
  }
});
