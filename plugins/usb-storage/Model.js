// Presentation only. Device labels are untrusted plain text, never shell code.
function capacity(bytes) {
  var units = ["B", "KiB", "MiB", "GiB", "TiB"];
  var value = Number(bytes || 0), index = 0;
  while (value >= 1024 && index < units.length - 1) { value /= 1024; index++; }
  return value.toFixed(index > 1 ? 1 : 0) + " " + units[index];
}
function title(drive) {
  var labels = (drive.volumes || []).map(function(v) { return v.label; }).filter(function(v) { return v !== ""; });
  return labels.length ? labels.join(", ") : (drive.name || "USB drive");
}
function detail(drive) {
  var volumes = drive.volumes || [];
  var mounted = volumes.filter(function(v) { return v.mounted; }).length;
  return capacity(drive.size) + " · " + (mounted ? mounted + " mounted" : "Not mounted")
    + (drive.siblingCount > 1 ? " · " + drive.siblingCount + " linked drives" : "");
}
function notifyText(text) {
  return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}
// Also runnable under Node for dependency-free unit tests.
if (typeof module !== "undefined") module.exports = { capacity, title, detail, notifyText };
