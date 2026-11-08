function number(value) {
  if (value === "" || value === undefined || value === null) return null
  var result = Number(value)
  return Number.isFinite(result) && result >= 0 ? result : null
}

function parse(output) {
  var state = { totalKiB: null, availableKiB: null, swapTotalKiB: null,
                swapFreeKiB: null, cpus: [], sensors: [] }
  String(output || "").split("\n").forEach(function(line) {
    var row = line.split("\t")
    if (row[0] === "M" && row.length >= 5) {
      state.totalKiB = number(row[1]); state.availableKiB = number(row[2])
      state.swapTotalKiB = number(row[3]); state.swapFreeKiB = number(row[4])
    } else if (row[0] === "C" && row.length >= 3 && state.cpus.length < 512) {
      var cpu = number(row[1]), khz = number(row[2])
      var maxKhz = number(row[3]), limitKhz = number(row[4])
      if (cpu !== null && khz !== null && khz <= 10000000)
        state.cpus.push({ id: cpu, ghz: khz / 1000000,
          maxGhz: maxKhz !== null && maxKhz <= 10000000 ? maxKhz / 1000000 : null,
          limitGhz: limitKhz !== null && limitKhz <= 10000000 ? limitKhz / 1000000 : null })
    } else if (row[0] === "T" && row.length >= 4 && state.sensors.length < 64) {
      var mc = number(row[3])
      if (mc !== null && mc <= 150000)
        state.sensors.push({ chip: row[1].slice(0, 80), label: row[2].slice(0, 80), celsius: mc / 1000 })
    }
  })
  state.cpus.sort(function(a, b) { return a.id - b.id })
  return state
}

function primaryTemperature(sensors) {
  var best = null, bestScore = -1
  sensors.forEach(function(sensor) {
    var chip = sensor.chip.toLowerCase(), label = sensor.label.toLowerCase(), score = 0
    if (chip === "coretemp") score = 3
    if (chip === "k10temp" || chip === "zenpower") score = 3
    if (/package|tdie/.test(label)) score += 3
    else if (/tctl|core/.test(label)) score += 1
    if (score > bestScore && score >= 3) { best = sensor; bestScore = score }
  })
  return best
}

function averageGhz(cpus) {
  if (!cpus.length) return null
  return cpus.reduce(function(total, cpu) { return total + cpu.ghz }, 0) / cpus.length
}

function highestGhz(cpus, field) {
  var values = cpus.map(function(cpu) { return cpu[field] }).filter(function(value) { return value !== null })
  return values.length ? Math.max.apply(null, values) : null
}

function gib(kib) { return kib === null ? "—" : (kib / 1048576).toFixed(1) + " GiB" }
function ghz(value) { return value === null ? "—" : value.toFixed(2) + " GHz" }
function degrees(value) { return value === null ? "—" : value.toFixed(0) + "°C" }

function barText(reading) {
  var used = reading.totalKiB === null || reading.availableKiB === null
    ? null : Math.max(0, reading.totalKiB - reading.availableKiB)
  var usedText = used === null ? "—" : (used / 1048576).toFixed(1) + "G"
  var speed = averageGhz(reading.cpus)
  var sensor = primaryTemperature(reading.sensors)
  return usedText + " · " + (speed === null ? "—" : speed.toFixed(2) + "GHz")
    + " · " + (sensor ? degrees(sensor.celsius) : "—°C")
}
