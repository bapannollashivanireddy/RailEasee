"use strict";

const STORAGE_KEY = "railease-web-v1";
const CLASS_NAMES = ["SL", "3A", "2A", "1A"];
const QUOTAS = ["GENERAL", "TATKAL", "LADIES", "SENIOR"];
const FARES = { SL: 500, "3A": 1200, "2A": 1800, "1A": 2500 };
const PAGE_META = {
  overview: ["Good morning", "OVERVIEW"],
  book: ["Book a ticket", "BOOK A TICKET"],
  bookings: ["Manage bookings", "MANAGE BOOKINGS"],
  trains: ["Train directory", "TRAIN DIRECTORY"],
  admin: ["Admin & demo tools", "MANAGEMENT"]
};

const view = document.getElementById("view");
const toastElement = document.getElementById("toast");
let toastTimer;
let storageWarning = "";
let state = loadState();
const undoStack = [];

function dateAfter(days) {
  const date = new Date();
  date.setDate(date.getDate() + days);
  return [date.getFullYear(), String(date.getMonth() + 1).padStart(2, "0"),
    String(date.getDate()).padStart(2, "0")].join("-");
}

function makeDefaultTrain() {
  return {
    number: "12001",
    name: "RailEase Express",
    stations: ["Alpha", "Beta", "Gamma", "Delta"],
    racLimit: 10,
    wlLimit: 20,
    classes: {
      SL: { seats: 10, cancellationRate: 0.08 },
      "3A": { seats: 0, cancellationRate: 0.08 },
      "2A": { seats: 0, cancellationRate: 0.08 },
      "1A": { seats: 0, cancellationRate: 0.08 }
    }
  };
}

function freshState() {
  return {
    page: "overview",
    trains: [makeDefaultTrain()],
    bookings: [],
    events: [{
      message: "RailEase is ready. Sample train 12001 is available.",
      time: Date.now()
    }],
    nextPnr: 1000000000,
    search: { source: "Alpha", destination: "Delta", date: dateAfter(7), travelClass: "SL" },
    searchDone: true,
    selectedTrain: null,
    lastLookup: ""
  };
}

function loadState() {
  try {
    const saved = localStorage.getItem(STORAGE_KEY);
    if (!saved) return freshState();
    const parsed = JSON.parse(saved);
    if (!Array.isArray(parsed.trains) || !Array.isArray(parsed.bookings) ||
        !Array.isArray(parsed.events) || !Number.isSafeInteger(parsed.nextPnr)) {
      throw new Error("Saved RailEase data has an unexpected shape.");
    }
    const defaults = freshState();
    return {
      ...defaults,
      ...parsed,
      page: "overview",
      search: parsed.search || defaults.search,
      searchDone: true,
      selectedTrain: null,
      lastLookup: ""
    };
  } catch (error) {
    console.error("Could not load RailEase browser data.", error);
    storageWarning = "Saved browser data could not be read. A fresh demo was opened.";
    return freshState();
  }
}

function persist() {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify({
      trains: state.trains,
      bookings: state.bookings,
      events: state.events.slice(-40),
      nextPnr: state.nextPnr,
      search: state.search
    }));
    return true;
  } catch (error) {
    console.error("Could not save RailEase browser data.", error);
    showToast("Changes are active, but this browser could not save them.", true);
    return false;
  }
}

function escapeHtml(value) {
  return String(value == null ? "" : value).replace(/[&<>"']/g, (character) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;"
  })[character]);
}

function showToast(message, isError) {
  toastElement.textContent = message;
  toastElement.classList.toggle("error", Boolean(isError));
  toastElement.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toastElement.classList.remove("show"), 3400);
}

function formatDate(value, options) {
  if (!value) return "—";
  const date = new Date(`${value}T12:00:00`);
  if (Number.isNaN(date.getTime())) return value;
  return new Intl.DateTimeFormat(undefined, options || {
    month: "short", day: "numeric", year: "numeric"
  }).format(date);
}

function formatTime(value) {
  return new Intl.DateTimeFormat(undefined, { hour: "numeric", minute: "2-digit" })
    .format(new Date(value));
}

function todayLabel() {
  return new Intl.DateTimeFormat(undefined, {
    weekday: "short", month: "short", day: "numeric"
  }).format(new Date());
}

function findTrain(number) {
  return state.trains.find((train) => train.number === String(number));
}

function enabledClasses(train) {
  return CLASS_NAMES.filter((name) => train.classes[name] && train.classes[name].seats > 0);
}

function activeBookings() {
  return state.bookings.filter((booking) => booking.status !== "CANCELLED");
}

function tripBookings(trainNumber, date, travelClass) {
  return activeBookings().filter((booking) =>
    booking.trainNumber === String(trainNumber) &&
    booking.date === date &&
    booking.travelClass === travelClass);
}

function stationIndex(train, station) {
  return train.stations.findIndex((value) =>
    value.toLowerCase() === String(station).toLowerCase());
}

function routeRange(train, source, destination) {
  const from = stationIndex(train, source);
  const to = stationIndex(train, destination);
  return from >= 0 && to > from ? { from, to } : null;
}

function segmentMask(from, to) {
  return ((1 << (to - from)) - 1) << from;
}

function seatMasks(train, date, travelClass) {
  const config = train.classes[travelClass];
  const masks = Array(config ? config.seats : 0).fill(0);
  tripBookings(train.number, date, travelClass)
    .filter((booking) => booking.status === "CNF" && booking.seatNumber > 0)
    .forEach((booking) => {
      const range = routeRange(train, booking.source, booking.destination);
      const seatIndex = booking.seatNumber - 1;
      if (range && seatIndex >= 0 && seatIndex < masks.length) {
        masks[seatIndex] |= segmentMask(range.from, range.to);
      }
    });
  return masks;
}

function findSeat(train, date, travelClass, source, destination) {
  const range = routeRange(train, source, destination);
  if (!range) return -1;
  const requested = segmentMask(range.from, range.to);
  const masks = seatMasks(train, date, travelClass);
  return masks.findIndex((mask) => (mask & requested) === 0);
}

function quotaRank(quota) {
  const index = QUOTAS.indexOf(quota);
  return index < 0 ? 0 : index;
}

function racQueue(trainNumber, date, travelClass) {
  return tripBookings(trainNumber, date, travelClass)
    .filter((booking) => booking.status === "RAC")
    .sort((a, b) => a.timestamp - b.timestamp);
}

function waitingQueue(trainNumber, date, travelClass) {
  return tripBookings(trainNumber, date, travelClass)
    .filter((booking) => booking.status === "WL")
    .sort((a, b) => quotaRank(a.quota) - quotaRank(b.quota) ||
      a.timestamp - b.timestamp);
}

function refreshPositions(trainNumber, date, travelClass) {
  racQueue(trainNumber, date, travelClass).forEach((booking, index) => {
    booking.queuePosition = index + 1;
  });
  waitingQueue(trainNumber, date, travelClass).forEach((booking, index) => {
    booking.queuePosition = index + 1;
  });
}

function emitStatus(booking, message) {
  addEvent(message || `PNR ${booking.pnr} status changed to ${booking.status}.`);
}

function addEvent(message) {
  state.events.unshift({ message, time: Date.now() });
  state.events = state.events.slice(0, 40);
}

function pushUndo() {
  undoStack.push({
    bookings: JSON.parse(JSON.stringify(state.bookings)),
    nextPnr: state.nextPnr,
    events: JSON.parse(JSON.stringify(state.events))
  });
  if (undoStack.length > 10) undoStack.shift();
}

function tripCounts(train, date, travelClass, source, destination) {
  const bookings = tripBookings(train.number, date, travelClass);
  const relevantSource = source || train.stations[0];
  const relevantDestination = destination || train.stations[train.stations.length - 1];
  let free = 0;
  const masks = seatMasks(train, date, travelClass);
  const range = routeRange(train, relevantSource, relevantDestination);
  if (range) {
    const requested = segmentMask(range.from, range.to);
    free = masks.filter((mask) => (mask & requested) === 0).length;
  }
  return {
    confirmed: bookings.filter((booking) => booking.status === "CNF").length,
    rac: bookings.filter((booking) => booking.status === "RAC").length,
    waiting: bookings.filter((booking) => booking.status === "WL").length,
    free,
    total: (train.classes[travelClass] || { seats: 0 }).seats
  };
}

function confirmationEstimate(booking, train) {
  const counts = tripCounts(train, booking.date, booking.travelClass,
    booking.source, booking.destination);
  const rate = train.classes[booking.travelClass].cancellationRate || 0;
  const days = Math.max(0, daysUntil(booking.date));
  const expected = rate * counts.confirmed * Math.min(days / 30, 1);
  const budget = counts.free + expected;
  const position = Math.max(1, booking.queuePosition || 1);
  const percent = budget + position === 0 ? 0 :
    Math.round(100 * budget / (budget + position));
  return {
    percent,
    label: percent >= 67 ? "High" : percent >= 34 ? "Medium" : "Low",
    expected: Math.round(expected * 10) / 10
  };
}

function statusBadge(status) {
  const className = status.toLowerCase();
  const label = status === "CANCELLED" ? "CANCELLED" : status;
  return `<span class="queue-status ${className}">${label}</span>`;
}

function availableStationOptions(selected) {
  const stations = [...new Set(state.trains.flatMap((train) => train.stations))].sort();
  return stations.map((station) =>
    `<option value="${escapeHtml(station)}" ${station === selected ? "selected" : ""}>${escapeHtml(station)}</option>`
  ).join("");
}

function classOptions(selected, train) {
  const classes = train ? enabledClasses(train) : CLASS_NAMES;
  return classes.map((name) =>
    `<option value="${name}" ${name === selected ? "selected" : ""}>${name}</option>`
  ).join("");
}

function renderStat(label, value, note, icon, color) {
  return `<article class="stat-card">
    <span class="stat-icon ${color}">${icon}</span>
    <div class="stat-copy"><div class="stat-label">${label}</div>
      <div class="stat-value">${value}<span class="stat-sub">${note || ""}</span></div>
    </div>
  </article>`;
}

function renderJourney(train, date, travelClass) {
  if (!train || !train.classes[travelClass] || !train.classes[travelClass].seats) {
    return `<div class="empty-state"><strong>No journey selected</strong>Choose an enabled train class to see live occupancy.</div>`;
  }
  const source = train.stations[0];
  const destination = train.stations[train.stations.length - 1];
  const counts = tripCounts(train, date, travelClass, source, destination);
  const totalPlaces = Math.max(1, counts.total + train.racLimit + train.wlLimit);
  const cnfWidth = counts.confirmed / totalPlaces * 100;
  const racWidth = counts.rac / totalPlaces * 100;
  const wlWidth = counts.waiting / totalPlaces * 100;
  return `<div class="journey-card">
    <div class="journey-top">
      <div class="journey-title"><span class="train-badge">↗</span><div>
        <strong>${escapeHtml(train.name)}</strong>
        <small>${escapeHtml(train.number)} · ${travelClass} · ${formatDate(date)}</small>
      </div></div>
      <span class="soft-badge"><span class="online-dot"></span> Live</span>
    </div>
    <div class="route-visual">
      <div class="route-city"><strong>${escapeHtml(source)}</strong><span>Origin</span></div>
      <div class="route-track"><span>${Math.max(1, train.stations.length - 1)} stops</span></div>
      <div class="route-city"><strong>${escapeHtml(destination)}</strong><span>Destination</span></div>
    </div>
    <div style="padding:0 16px 15px">
      <div class="occupancy-row"><span>Journey occupancy</span><span>${counts.confirmed} / ${counts.total} seats confirmed</span></div>
      <div class="occupancy-bar" aria-label="Occupancy breakdown">
        <span class="cnf" style="width:${cnfWidth}%"></span>
        <span class="rac" style="width:${racWidth}%"></span>
        <span class="wl" style="width:${wlWidth}%"></span>
      </div>
      <div class="occupancy-legend">
        <span><i class="legend-dot cnf"></i>${counts.confirmed} Confirmed</span>
        <span><i class="legend-dot rac"></i>${counts.rac} RAC</span>
        <span><i class="legend-dot wl"></i>${counts.waiting} Waiting list</span>
        <span>${counts.free} seats open for full route</span>
      </div>
    </div>
  </div>`;
}

function renderQueue(trainNumber, date, travelClass, maxRows) {
  const train = findTrain(trainNumber);
  if (!train) return `<div class="empty-state">No train selected.</div>`;
  const entries = [
    ...racQueue(trainNumber, date, travelClass).map((booking) => ({ booking, status: "RAC" })),
    ...waitingQueue(trainNumber, date, travelClass).map((booking) => ({ booking, status: "WL" }))
  ].slice(0, maxRows || 7);
  if (!entries.length) {
    return `<div class="empty-state"><strong>Queues are clear</strong>New RAC and waiting-list bookings appear here.</div>`;
  }
  return `<div class="queue-list">${entries.map(({ booking, status }) => `
    <div class="queue-row">
      <span class="queue-rank">${booking.queuePosition}</span>
      <div><div class="queue-name">${escapeHtml(booking.name)}</div>
        <div class="queue-detail">${escapeHtml(booking.pnr)} · ${escapeHtml(booking.source)}–${escapeHtml(booking.destination)}</div>
      </div>
      <span class="queue-status ${status.toLowerCase()}">${status}</span>
    </div>`).join("")}</div>`;
}

function renderActivity(limit) {
  const events = state.events.slice(0, limit || 5);
  if (!events.length) return `<div class="empty-state">No status changes yet.</div>`;
  return `<div class="activity-list">${events.map((event) => `
    <div class="activity-row">
      <span class="activity-mark">✓</span>
      <div class="activity-text">${escapeHtml(event.message)}<time>${formatTime(event.time)}</time></div>
    </div>`).join("")}</div>`;
}

function renderRecentBookings(limit) {
  const items = activeBookings().slice().sort((a, b) => b.timestamp - a.timestamp).slice(0, limit || 5);
  if (!items.length) return `<div class="empty-state"><strong>No bookings yet</strong>Book a ticket to see passenger status here.</div>`;
  return `<div class="table-wrap"><table class="data-table">
    <thead><tr><th>Passenger</th><th>Journey</th><th>PNR status</th><th>Booking</th></tr></thead>
    <tbody>${items.map((booking) => `<tr>
      <td><span class="table-primary">${escapeHtml(booking.name)}</span><span class="table-secondary">PNR ${escapeHtml(booking.pnr)}</span></td>
      <td><span class="table-primary">${escapeHtml(booking.source)} → ${escapeHtml(booking.destination)}</span><span class="table-secondary">${formatDate(booking.date)} · ${escapeHtml(booking.travelClass)}</span></td>
      <td>${statusBadge(booking.status)}${booking.status === "CNF" ? `<span class="table-secondary">Seat ${booking.seatNumber}</span>` : `<span class="table-secondary">Position ${booking.queuePosition}</span>`}</td>
      <td>${formatTime(booking.timestamp)}</td>
    </tr>`).join("")}</tbody>
  </table></div>`;
}

function renderOverview() {
  const bookings = activeBookings();
  const confirmed = bookings.filter((booking) => booking.status === "CNF").length;
  const rac = bookings.filter((booking) => booking.status === "RAC").length;
  const waiting = bookings.filter((booking) => booking.status === "WL").length;
  const train = findTrain("12001") || state.trains[0];
  const date = state.search.date || dateAfter(7);
  const travelClass = train && enabledClasses(train).includes("SL") ? "SL" :
    (train ? enabledClasses(train)[0] : "SL");
  const journey = train ? renderJourney(train, date, travelClass) :
    `<div class="empty-state">Add a train from Admin to start.</div>`;
  const routeTrain = train || { number: "12001" };
  return `<section class="hero">
    <div class="hero-copy">
      <div class="hero-kicker">A BETTER WAY TO GET THERE</div>
      <h2>Every journey, a little more certain.</h2>
      <p>Find a train, reserve a seat, and let RailEase handle the queue.</p>
      <div class="hero-tags"><span class="hero-tag">Live RAC &amp; WL tracking</span><span class="hero-tag">Automatic seat reallocation</span></div>
    </div>
    <div class="hero-art" aria-hidden="true"><div class="route-art"><span class="route-line"></span>
      <span class="route-stop first" data-label="Origin"></span><span class="route-stop middle"></span>
      <span class="route-stop last" data-label="Arrival"></span><span class="route-train">↗</span>
    </div></div>
    <form class="quick-search" id="quick-search-form">
      <div class="field"><label for="quick-source">FROM</label><select id="quick-source" name="source">${availableStationOptions(state.search.source)}</select></div>
      <div class="field"><label for="quick-destination">TO</label><select id="quick-destination" name="destination">${availableStationOptions(state.search.destination)}</select></div>
      <div class="field"><label for="quick-date">JOURNEY DATE</label><input id="quick-date" type="date" name="date" value="${escapeHtml(state.search.date)}" required></div>
      <div class="field"><label for="quick-class">CLASS</label><select id="quick-class" name="travelClass">${classOptions(state.search.travelClass)}</select></div>
      <button class="button button-primary" type="submit">Find trains <span aria-hidden="true">→</span></button>
    </form>
  </section>

  <section class="stat-grid" aria-label="Booking summary">
    ${renderStat("Active bookings", bookings.length, "all journeys", "▤", "blue")}
    ${renderStat("Confirmed seats", confirmed, "CNF", "✓", "green")}
    ${renderStat("RAC queue", rac, "awaiting a seat", "↗", "amber")}
    ${renderStat("Waiting list", waiting, "priority tracked", "☷", "violet")}
  </section>

  <section class="content-grid">
    <div class="stack">
      <article class="panel">
        <div class="panel-header"><div><h3 class="section-title">Journey overview</h3><p class="section-note">A live view of seats and passenger queues.</p></div>
          <button class="link-button" type="button" data-page-link="trains">Train directory →</button></div>
        ${journey}
      </article>
      <article class="panel">
        <div class="panel-header"><div><h3 class="section-title">Recent bookings</h3><p class="section-note">Your latest passenger updates.</p></div>
          <button class="link-button" type="button" data-page-link="bookings">View all →</button></div>
        ${renderRecentBookings(5)}
      </article>
    </div>
    <div class="stack">
      <article class="panel">
        <div class="panel-header"><div><h3 class="section-title">Live queue</h3><p class="section-note">${escapeHtml(routeTrain.number)} · ${formatDate(date)} · ${travelClass}</p></div>
          <span class="soft-badge">Auto-updating</span></div>
        ${train ? renderQueue(train.number, date, travelClass, 6) : `<div class="empty-state">No queue yet.</div>`}
      </article>
      <article class="panel">
        <div class="panel-header"><div><h3 class="section-title">Latest activity</h3><p class="section-note">Status changes and confirmations.</p></div></div>
        ${renderActivity(5)}
      </article>
    </div>
  </section>`;
}

function getSearchResults(search) {
  return state.trains.filter((train) =>
    routeRange(train, search.source, search.destination) &&
    train.classes[search.travelClass] &&
    train.classes[search.travelClass].seats > 0);
}

function renderTrainResult(train, search) {
  const counts = tripCounts(train, search.date, search.travelClass, search.source, search.destination);
  const selected = state.selectedTrain === train.number;
  return `<article class="train-result">
    <div class="train-result-head">
      <div><strong>${escapeHtml(train.name)}</strong><small>${escapeHtml(train.number)} · ${escapeHtml(search.source)} → ${escapeHtml(search.destination)} · ${formatDate(search.date)}</small></div>
      <span class="soft-badge">${counts.free} seats open</span>
    </div>
    <div class="class-pills"><span class="class-pill">${escapeHtml(search.travelClass)} <b>${counts.free ? "Available" : counts.rac < train.racLimit ? "RAC available" : "WL " + (counts.waiting + 1)}</b></span>
      <span class="class-pill">RAC <b>${Math.max(0, train.racLimit - counts.rac)} open</b></span>
      <span class="class-pill">WL <b>${Math.max(0, train.wlLimit - counts.waiting)} open</b></span></div>
    <button class="button ${selected ? "button-secondary" : "button-primary"} button-small" type="button" data-select-train="${escapeHtml(train.number)}">
      ${selected ? "Selected" : "Choose train"} <span aria-hidden="true">→</span>
    </button>
  </article>`;
}

function renderBookingForm() {
  const train = findTrain(state.selectedTrain);
  if (!train) return "";
  const search = state.search;
  return `<article class="panel form-panel" id="passenger-form-panel">
    <div class="panel-title-row"><span class="panel-title-icon">＋</span><div><h3>Passenger details</h3>
      <p>Booking for ${escapeHtml(train.name)} · ${escapeHtml(train.number)}</p></div></div>
    <div class="selected-train"><div><strong>${escapeHtml(search.source)} → ${escapeHtml(search.destination)}</strong>
      <span>${formatDate(search.date)} · ${escapeHtml(search.travelClass)} · queues are assigned automatically</span></div>
      <span class="soft-badge">${tripCounts(train, search.date, search.travelClass, search.source, search.destination).free} seats open</span>
    </div>
    <form id="booking-form">
      <div class="form-grid">
        <div class="field"><label for="passenger-name">PASSENGER NAME</label><input id="passenger-name" name="name" maxlength="63" placeholder="e.g. Arun Kumar" required></div>
        <div class="field"><label for="passenger-phone">PHONE</label><input id="passenger-phone" name="phone" type="tel" maxlength="23" placeholder="Mobile number" required></div>
        <div class="field"><label for="passenger-age">AGE</label><input id="passenger-age" name="age" type="number" min="1" max="120" value="28" required></div>
        <div class="field"><label for="passenger-gender">GENDER</label><select id="passenger-gender" name="gender"><option value="M">Male</option><option value="F">Female</option><option value="O">Other</option></select></div>
        <div class="field"><label for="passenger-berth">BERTH PREFERENCE</label><select id="passenger-berth" name="berth"><option>Any</option><option>Lower</option><option>Middle</option><option>Upper</option></select></div>
        <div class="field"><label for="passenger-quota">QUOTA</label><select id="passenger-quota" name="quota">${QUOTAS.map((quota) => `<option value="${quota}">${quota}</option>`).join("")}</select></div>
      </div>
      <div class="form-actions"><button class="button button-primary" type="submit">Confirm booking <span aria-hidden="true">→</span></button>
        <span class="muted small">Demo fare: ${formatMoney(FARES[search.travelClass])} · no payment collected</span></div>
    </form>
  </article>`;
}

function renderBookPage() {
  const search = state.search;
  const results = state.searchDone ? getSearchResults(search) : [];
  return `<div class="page-heading"><div><h2>Find your next train</h2><p>Search the timetable, then choose a train and enter passenger details.</p></div></div>
  <section class="page-two-col">
    <div class="stack">
      <article class="panel form-panel">
        <div class="panel-title-row"><span class="panel-title-icon">⌕</span><div><h3>Search trains</h3><p>Check availability for your exact route and date.</p></div></div>
        <form id="train-search-form">
          <div class="form-grid">
            <div class="field"><label for="search-source">FROM</label><select id="search-source" name="source">${availableStationOptions(search.source)}</select></div>
            <div class="field"><label for="search-destination">TO</label><select id="search-destination" name="destination">${availableStationOptions(search.destination)}</select></div>
            <div class="field"><label for="search-date">JOURNEY DATE</label><input id="search-date" type="date" name="date" value="${escapeHtml(search.date)}" required></div>
            <div class="field"><label for="search-class">CLASS</label><select id="search-class" name="travelClass">${classOptions(search.travelClass)}</select></div>
          </div>
          <div class="form-actions"><button class="button button-primary" type="submit">Search availability <span aria-hidden="true">→</span></button></div>
        </form>
      </article>
      <article class="panel">
        <div class="section-heading"><div><h3 class="section-title">Matching trains</h3><p class="section-note">${results.length} option${results.length === 1 ? "" : "s"} for ${escapeHtml(search.source)} → ${escapeHtml(search.destination)}</p></div></div>
        ${results.length ? `<div class="result-list">${results.map((train) => renderTrainResult(train, search)).join("")}</div>` :
          `<div class="empty-state"><strong>No trains found</strong>Try a different station pair, date, or class.</div>`}
      </article>
      ${renderBookingForm()}
    </div>
    <div class="stack">
      <article class="panel">
        <div class="panel-header"><div><h3 class="section-title">How queues work</h3><p class="section-note">Your place is tracked in real time.</p></div></div>
        <div class="queue-row"><span class="queue-rank">1</span><div><div class="queue-name">Confirmed</div><div class="queue-detail">A seat is assigned on your segment.</div></div><span class="queue-status cnf">CNF</span></div>
        <div class="queue-row"><span class="queue-rank">2</span><div><div class="queue-name">Reservation Against Cancellation</div><div class="queue-detail">FIFO queue; promoted when a compatible seat opens.</div></div><span class="queue-status rac">RAC</span></div>
        <div class="queue-row"><span class="queue-rank">3</span><div><div class="queue-name">Waiting list</div><div class="queue-detail">Quota priority, then booking time.</div></div><span class="queue-status wl">WL</span></div>
      </article>
      <article class="panel">
        <div class="panel-header"><div><h3 class="section-title">Smart probability</h3><p class="section-note">A guide based on historical class cancellation rates.</p></div><span class="stat-icon green">↗</span></div>
        <div class="callout"><span class="callout-icon">i</span><div>RailEase combines available seats with expected cancellations and your live queue position. Estimates are illustrative, not a guarantee.</div></div>
      </article>
    </div>
  </section>`;
}

function estimateMarkup(booking) {
  const train = findTrain(booking.trainNumber);
  if (!train || (booking.status !== "WL" && booking.status !== "RAC")) return "";
  const estimate = confirmationEstimate(booking, train);
  return `<div class="confidence">
    <span class="confidence-ring" style="--progress:${estimate.percent}%" data-value="${estimate.percent}%"></span>
    <div class="confidence-copy"><strong>${estimate.label} confirmation chance · ${estimate.percent}%</strong>
      <span>Current ${booking.status} position ${booking.queuePosition}; estimated cancellations ${estimate.expected}.</span></div>
  </div>`;
}

function renderLookup(booking) {
  if (!booking) return `<div class="empty-state"><strong>Look up a booking</strong>Enter a 10-digit PNR to see its live status and queue position.</div>`;
  const train = findTrain(booking.trainNumber);
  const isActive = booking.status !== "CANCELLED";
  const position = booking.status === "CNF" ? `Seat ${booking.seatNumber}` :
    booking.status === "CANCELLED" ? "Cancelled" : `Live position ${booking.queuePosition}`;
  return `<article class="pnr-card">
    <div class="pnr-card-top"><div><div class="small muted">PASSENGER / PNR</div><div class="pnr-code">${escapeHtml(booking.pnr)}</div><div class="pnr-meta">${escapeHtml(booking.name)} · ${escapeHtml(booking.phone)}</div></div>${statusBadge(booking.status)}</div>
    <div class="pnr-route">${escapeHtml(booking.source)} <span class="muted">→</span> ${escapeHtml(booking.destination)}</div>
    <div class="pnr-meta"><span>${escapeHtml(train ? train.name : booking.trainNumber)}</span><span>${formatDate(booking.date)}</span><span>${escapeHtml(booking.travelClass)} · ${position}</span></div>
    ${estimateMarkup(booking)}
    ${booking.status === "CANCELLED" ? `<div class="callout" style="margin-top:14px"><span class="callout-icon">✓</span><div>Cancelled · estimated refund ${formatMoney(booking.refund || 0)}</div></div>` : ""}
    ${isActive ? `<div class="form-actions"><button class="button button-danger button-small" type="button" data-cancel-booking="${escapeHtml(booking.pnr)}">Cancel ticket</button></div>` : ""}
  </article>`;
}

function renderBookingsPage() {
  const booking = state.lastLookup ?
    state.bookings.find((item) => item.pnr === state.lastLookup) : null;
  return `<div class="page-heading"><div><h2>Manage your bookings</h2><p>Check live PNR status, estimate confirmation, or cancel a reservation.</p></div></div>
  <section class="two-panels">
    <article class="panel">
      <div class="panel-header"><div><h3 class="section-title">PNR status</h3><p class="section-note">A live position is shown for RAC and waiting-list passengers.</p></div></div>
      <form class="pnr-search" id="pnr-search-form">
        <div class="field"><label for="pnr-input">10-DIGIT PNR</label><input id="pnr-input" name="pnr" inputmode="numeric" pattern="[0-9]{10}" maxlength="10" placeholder="e.g. 1000000000" value="${escapeHtml(state.lastLookup)}" required></div>
        <button class="button button-primary" type="submit">Check status</button>
      </form>
      ${booking ? renderLookup(booking) : `<div id="lookup-result"></div>`}
    </article>
    <article class="panel">
      <div class="panel-header"><div><h3 class="section-title">Queue snapshot</h3><p class="section-note">Next passengers in line by quota and booking time.</p></div></div>
      ${state.trains.length ? renderQueue(state.trains[0].number, state.search.date,
        enabledClasses(state.trains[0])[0] || "SL", 6) : `<div class="empty-state">Add a train to begin.</div>`}
    </article>
  </section>
  <article class="panel" style="margin-top:18px">
    <div class="panel-header"><div><h3 class="section-title">All active bookings</h3><p class="section-note">${activeBookings().length} reservations in this browser demo.</p></div>
      <button class="link-button" type="button" data-page-link="book">Book another →</button></div>
    ${renderRecentBookings(100)}
  </article>`;
}

function renderTrainCard(train) {
  const classes = enabledClasses(train);
  const seats = classes.reduce((sum, name) => sum + train.classes[name].seats, 0);
  const occupied = activeBookings().filter((booking) =>
    booking.trainNumber === train.number && booking.status === "CNF").length;
  return `<article class="train-card">
    <div class="train-card-top"><div><strong>${escapeHtml(train.name)}</strong><small>${escapeHtml(train.number)} · ${train.stations.length} stations</small></div><span class="train-badge">↗</span></div>
    <div class="train-route">${train.stations.map(escapeHtml).join(" <span class='muted'>→</span> ")}</div>
    <div class="progress-track"><span style="width:${seats ? Math.min(100, occupied / seats * 100) : 0}%"></span></div>
    <div class="capacity-strip"><div><b>${classes.join(", ") || "—"}</b>Classes</div><div><b>${Math.max(0, seats - occupied)} / ${seats}</b>Seats open*</div><div><b>${train.racLimit} / ${train.wlLimit}</b>RAC / WL</div></div>
    <p class="muted small">*Across active journey dates and segments.</p>
  </article>`;
}

function renderTrainsPage() {
  return `<div class="page-heading"><div><h2>Train directory</h2><p>Browse routes, enabled classes, and configured reservation capacity.</p></div>
    <button class="button button-primary" type="button" data-page-link="admin">Manage trains <span aria-hidden="true">→</span></button></div>
    ${state.trains.length ? `<section class="train-card-grid">${state.trains.map(renderTrainCard).join("")}</section>` :
      `<div class="panel empty-state"><strong>No trains yet</strong>Add a train from the admin page.</div>`}`;
}

function renderTrainAdminRow(train) {
  const active = activeBookings().filter((booking) => booking.trainNumber === train.number).length;
  return `<div class="train-admin-row">
    <div><span class="table-primary">${escapeHtml(train.number)} · ${escapeHtml(train.name)}</span>
      <span class="table-secondary">${train.stations.map(escapeHtml).join(" → ")} · ${active} active booking${active === 1 ? "" : "s"}</span></div>
    <button class="button button-secondary button-small" type="button" data-edit-train="${escapeHtml(train.number)}">Edit</button>
    <button class="button button-danger button-small" type="button" data-remove-train="${escapeHtml(train.number)}">Remove</button>
  </div>`;
}

function renderAdminPage() {
  const train = state.trains.find((item) => item.number === state.selectedTrain);
  const edit = train || null;
  const config = (name) => edit ? edit.classes[name].seats : (name === "SL" ? 10 : 0);
  const rate = (name) => edit ? Math.round(edit.classes[name].cancellationRate * 100) : 8;
  return `<div class="page-heading"><div><h2>Admin &amp; demo tools</h2><p>Configure trains, review the notification log, and exercise automated reallocation.</p></div>
    <button class="button button-secondary" type="button" data-undo ${undoStack.length ? "" : "disabled"}>↶ Undo last action</button></div>
  <section class="admin-grid">
    <div class="stack">
      <article class="panel form-panel">
        <div class="panel-title-row"><span class="panel-title-icon">⚙</span><div><h3>${edit ? "Update train" : "Add a train"}</h3>
          <p>Routes follow station order; a train with active bookings cannot be reconfigured.</p></div></div>
        <form id="train-admin-form">
          <div class="form-grid three">
            <div class="field"><label for="admin-number">TRAIN NUMBER</label><input id="admin-number" name="number" value="${edit ? escapeHtml(edit.number) : ""}" maxlength="10" required></div>
            <div class="field" style="grid-column:span 2"><label for="admin-name">TRAIN NAME</label><input id="admin-name" name="name" value="${edit ? escapeHtml(edit.name) : ""}" maxlength="47" required></div>
            <div class="field full-width"><label for="admin-stations">ROUTE STATIONS</label><input id="admin-stations" name="stations" value="${edit ? escapeHtml(edit.stations.join(", ")) : ""}" placeholder="Alpha, Beta, Gamma, Delta" required><span class="field-help">Enter 2–16 comma-separated station names in travel order.</span></div>
            ${CLASS_NAMES.map((name) => `<div class="field"><label for="seats-${name}">${name} SEATS</label><input id="seats-${name}" name="seats-${name}" type="number" min="0" max="2048" value="${config(name)}" required></div>`).join("")}
            <div class="field"><label for="admin-rac">RAC LIMIT</label><input id="admin-rac" name="racLimit" type="number" min="0" max="1000" value="${edit ? edit.racLimit : 10}" required></div>
            <div class="field"><label for="admin-wl">WAITING-LIST LIMIT</label><input id="admin-wl" name="wlLimit" type="number" min="0" max="1000" value="${edit ? edit.wlLimit : 20}" required></div>
          </div>
          <div class="separator"></div>
          <div class="section-heading"><div><h3 class="section-title">Historical cancellation rate</h3><p class="section-note">Illustrative percentage used by the probability estimate.</p></div></div>
          <div class="form-grid four">${CLASS_NAMES.map((name) => `<div class="field"><label for="rate-${name}">${name} RATE %</label><input id="rate-${name}" name="rate-${name}" type="number" min="0" max="100" value="${rate(name)}" required></div>`).join("")}</div>
          <div class="form-actions"><button class="button button-primary" type="submit">${edit ? "Save train changes" : "Add train"}</button>
            ${edit ? `<button class="button button-secondary" type="button" data-clear-edit>Cancel edit</button>` : ""}</div>
        </form>
      </article>
      <article class="panel">
        <div class="panel-header"><div><h3 class="section-title">Manage train directory</h3><p class="section-note">Remove is disabled when active reservations exist.</p></div></div>
        ${state.trains.length ? state.trains.map(renderTrainAdminRow).join("") : `<div class="empty-state">No trains have been added.</div>`}
      </article>
    </div>
    <div class="admin-tools">
      <article class="panel">
        <div class="panel-header"><div><h3 class="section-title">Reallocation demo</h3><p class="section-note">Cancel random confirmed tickets and watch RAC / WL move.</p></div><span class="stat-icon amber">↗</span></div>
        <form id="simulation-form">
          <div class="form-grid">
            <div class="field"><label for="sim-train">TRAIN</label><select id="sim-train" name="trainNumber">${state.trains.map((item) => `<option value="${escapeHtml(item.number)}">${escapeHtml(item.number)} · ${escapeHtml(item.name)}</option>`).join("")}</select></div>
            <div class="field"><label for="sim-class">CLASS</label><select id="sim-class" name="travelClass">${classOptions("SL", state.trains[0])}</select></div>
            <div class="field"><label for="sim-date">JOURNEY DATE</label><input id="sim-date" name="date" type="date" value="${escapeHtml(state.search.date)}" required></div>
            <div class="field"><label for="sim-count">CANCELLATIONS</label><input id="sim-count" name="count" type="number" min="1" max="100" value="5" required></div>
          </div>
          <div class="form-actions"><button class="button button-dark" type="submit">Run simulation</button></div>
        </form>
        <div class="separator"></div>
        <div class="callout"><span class="callout-icon">i</span><div>The simulation only cancels currently confirmed bookings. Queue priority is quota first, then booking timestamp.</div></div>
      </article>
      <article class="panel">
        <div class="panel-header"><div><h3 class="section-title">Notification log</h3><p class="section-note">Status changes from bookings, cancellations, and promotions.</p></div>
          <button class="link-button" type="button" data-export-log>Export log</button></div>
        ${renderActivity(8)}
      </article>
      <article class="panel">
        <div class="panel-header"><div><h3 class="section-title">Local demo data</h3><p class="section-note">Nothing is sent to a server. Data is saved in this browser.</p></div></div>
        <button class="button button-danger button-small" type="button" data-reset-demo>Reset demo data</button>
      </article>
    </div>
  </section>`;
}

function formatMoney(amount) {
  return new Intl.NumberFormat(undefined, {
    style: "currency", currency: "INR", maximumFractionDigits: 0
  }).format(amount);
}

function render() {
  const page = PAGE_META[state.page] ? state.page : "overview";
  state.page = page;
  document.getElementById("page-title").textContent = PAGE_META[page][0];
  document.getElementById("breadcrumb").textContent = PAGE_META[page][1];
  document.getElementById("today-label").textContent = todayLabel();
  document.querySelectorAll("[data-page-link]").forEach((button) => {
    button.classList.toggle("active", button.dataset.pageLink === page);
  });
  view.innerHTML = page === "book" ? renderBookPage() :
    page === "bookings" ? renderBookingsPage() :
    page === "trains" ? renderTrainsPage() :
    page === "admin" ? renderAdminPage() : renderOverview();
}

function setPage(page) {
  if (!PAGE_META[page]) return;
  state.page = page;
  if (page !== "admin") state.selectedTrain = null;
  render();
  window.scrollTo({ top: 0, behavior: "smooth" });
}

function searchFromForm(form) {
  const data = new FormData(form);
  const search = {
    source: String(data.get("source") || ""),
    destination: String(data.get("destination") || ""),
    date: String(data.get("date") || ""),
    travelClass: String(data.get("travelClass") || "SL")
  };
  if (!search.source || !search.destination || search.source === search.destination) {
    showToast("Choose two different stations in route order.", true);
    return false;
  }
  if (!validDate(search.date)) {
    showToast("Enter a valid journey date.", true);
    return false;
  }
  state.search = search;
  state.searchDone = true;
  state.selectedTrain = null;
  persist();
  return true;
}

function validDate(value) {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(value)) return false;
  const [year, month, day] = value.split("-").map(Number);
  const date = new Date(year, month - 1, day);
  return date.getFullYear() === year && date.getMonth() === month - 1 &&
    date.getDate() === day;
}

function daysUntil(value) {
  const [year, month, day] = value.split("-").map(Number);
  const target = new Date(year, month - 1, day);
  const today = new Date();
  today.setHours(0, 0, 0, 0);
  return Math.round((target - today) / 86400000);
}

function saveUndoableChange(successMessage) {
  persist();
  render();
  showToast(successMessage);
}

function makeBooking(form) {
  const train = findTrain(state.selectedTrain);
  if (!train) {
    showToast("Choose a train before entering passenger details.", true);
    return;
  }
  const data = new FormData(form);
  const name = String(data.get("name") || "").trim();
  const phone = String(data.get("phone") || "").trim();
  const age = Number(data.get("age"));
  const travelClass = state.search.travelClass;
  const route = routeRange(train, state.search.source, state.search.destination);
  if (!name || !phone || !Number.isInteger(age) || age < 1 || age > 120 ||
      !route || !validDate(state.search.date)) {
    showToast("Check the passenger details and journey route.", true);
    return;
  }
  const duplicate = activeBookings().some((booking) =>
    booking.trainNumber === train.number &&
    booking.date === state.search.date &&
    booking.name.toLowerCase() === name.toLowerCase() &&
    booking.phone === phone);
  if (duplicate) {
    showToast("This passenger already has an active booking for that train and date.", true);
    return;
  }
  const counts = tripCounts(train, state.search.date, travelClass,
    state.search.source, state.search.destination);
  const seat = findSeat(train, state.search.date, travelClass,
    state.search.source, state.search.destination);
  let status;
  let position = 0;
  if (seat >= 0) {
    status = "CNF";
  } else if (counts.rac < train.racLimit) {
    status = "RAC";
    position = counts.rac + 1;
  } else if (counts.waiting < train.wlLimit) {
    status = "WL";
    position = counts.waiting + 1;
  } else {
    showToast("This train, class, and journey segment are full.", true);
    return;
  }
  pushUndo();
  const pnr = String(state.nextPnr++);
  const booking = {
    pnr,
    name,
    age,
    gender: String(data.get("gender") || "O"),
    phone,
    berth: String(data.get("berth") || "Any"),
    trainNumber: train.number,
    date: state.search.date,
    source: state.search.source,
    destination: state.search.destination,
    travelClass,
    status,
    initialStatus: status,
    seatNumber: status === "CNF" ? seat + 1 : 0,
    queuePosition: position,
    originalPosition: position,
    timestamp: Date.now(),
    quota: String(data.get("quota") || "GENERAL")
  };
  state.bookings.push(booking);
  refreshPositions(train.number, booking.date, travelClass);
  emitStatus(booking, `PNR ${pnr} booked as ${status}${position ? ` ${position}` : ""}.`);
  state.lastLookup = pnr;
  state.page = "bookings";
  saveUndoableChange(`Ticket booked · PNR ${pnr} is ${status}${position ? ` ${position}` : ""}.`);
}

function refundFor(booking) {
  const days = daysUntil(booking.date);
  const fraction = days > 2 ? 1 : days > 1 ? 0.75 : 0.5;
  return Math.round((FARES[booking.travelClass] || 500) * fraction);
}

function promoteTrip(train, date, travelClass) {
  const maxSteps = tripBookings(train.number, date, travelClass).length + 1;
  let steps = 0;
  while (steps++ < maxSteps) {
    const rac = racQueue(train.number, date, travelClass);
    let racCandidate = null;
    let racSeat = -1;
    for (const candidate of rac) {
      const seat = findSeat(train, date, travelClass, candidate.source, candidate.destination);
      if (seat >= 0) {
        racCandidate = candidate;
        racSeat = seat;
        break;
      }
    }
    if (racCandidate) {
      racCandidate.status = "CNF";
      racCandidate.seatNumber = racSeat + 1;
      racCandidate.queuePosition = 0;
      emitStatus(racCandidate, `PNR ${racCandidate.pnr} promoted from RAC to CNF (seat ${racSeat + 1}).`);
      refreshPositions(train.number, date, travelClass);
      continue;
    }

    const waiting = waitingQueue(train.number, date, travelClass);
    if (!waiting.length) break;
    let waitingCandidate = null;
    let waitingSeat = -1;
    for (const candidate of waiting) {
      const seat = findSeat(train, date, travelClass, candidate.source, candidate.destination);
      if (seat >= 0) {
        waitingCandidate = candidate;
        waitingSeat = seat;
        break;
      }
    }
    if (waitingCandidate) {
      waitingCandidate.status = "CNF";
      waitingCandidate.seatNumber = waitingSeat + 1;
      waitingCandidate.queuePosition = 0;
      emitStatus(waitingCandidate, `PNR ${waitingCandidate.pnr} promoted from WL to CNF (seat ${waitingSeat + 1}).`);
      refreshPositions(train.number, date, travelClass);
      continue;
    }
    if (rac.length < train.racLimit) {
      const candidate = waiting[0];
      candidate.status = "RAC";
      candidate.seatNumber = 0;
      candidate.queuePosition = 0;
      candidate.timestamp = Date.now() + steps;
      emitStatus(candidate, `PNR ${candidate.pnr} promoted from WL to RAC.`);
      refreshPositions(train.number, date, travelClass);
      continue;
    }
    break;
  }
  refreshPositions(train.number, date, travelClass);
}

function cancelBooking(pnr, options) {
  const booking = state.bookings.find((item) => item.pnr === String(pnr));
  if (!booking || booking.status === "CANCELLED") {
    return { ok: false, message: "That PNR is not an active booking." };
  }
  const train = findTrain(booking.trainNumber);
  if (!train) return { ok: false, message: "The booking's train is no longer in the directory." };
  if (!options || !options.skipUndo) pushUndo();
  const refund = booking.status === "CNF" ? refundFor(booking) : 0;
  const previous = booking.status;
  booking.status = "CANCELLED";
  booking.seatNumber = 0;
  booking.queuePosition = 0;
  booking.refund = refund;
  booking.cancelledAt = Date.now();
  emitStatus(booking, `PNR ${booking.pnr} cancelled from ${previous}; estimated refund ${formatMoney(refund)}.`);
  if (previous === "CNF" || previous === "RAC") {
    promoteTrip(train, booking.date, booking.travelClass);
  }
  refreshPositions(booking.trainNumber, booking.date, booking.travelClass);
  return { ok: true, booking, refund };
}

function simulateCancellations(form) {
  const data = new FormData(form);
  const train = findTrain(String(data.get("trainNumber")));
  const travelClass = String(data.get("travelClass"));
  const date = String(data.get("date"));
  const requested = Number(data.get("count"));
  if (!train || !train.classes[travelClass] || !train.classes[travelClass].seats ||
      !validDate(date) || !Number.isInteger(requested) || requested < 1) {
    showToast("Choose a train, valid date, and cancellation count.", true);
    return;
  }
  const candidates = tripBookings(train.number, date, travelClass)
    .filter((booking) => booking.status === "CNF");
  if (!candidates.length) {
    showToast("No confirmed tickets are available to cancel for that trip.", true);
    return;
  }
  pushUndo();
  let cancelled = 0;
  while (cancelled < requested) {
    const eligible = tripBookings(train.number, date, travelClass)
      .filter((booking) => booking.status === "CNF");
    if (!eligible.length) break;
    const booking = eligible[Math.floor(Math.random() * eligible.length)];
    const result = cancelBooking(booking.pnr, { skipUndo: true });
    if (result.ok) cancelled += 1;
    else break;
  }
  addEvent(`Simulation cancelled ${cancelled} confirmed ticket${cancelled === 1 ? "" : "s"} on ${train.number}.`);
  state.search.date = date;
  state.search.travelClass = travelClass;
  saveUndoableChange(`Simulation complete · ${cancelled} cancellation${cancelled === 1 ? "" : "s"}.`);
}

function updateTrain(form) {
  const data = new FormData(form);
  const number = String(data.get("number") || "").trim();
  const name = String(data.get("name") || "").trim();
  const stations = String(data.get("stations") || "").split(",").map((item) => item.trim()).filter(Boolean);
  const seats = {};
  const classes = {};
  for (const className of CLASS_NAMES) {
    seats[className] = Number(data.get(`seats-${className}`));
    const rate = Number(data.get(`rate-${className}`));
    if (!Number.isInteger(seats[className]) || seats[className] < 0 ||
        !Number.isFinite(rate) || rate < 0 || rate > 100) {
      showToast(`Enter valid ${className} seat and cancellation-rate values.`, true);
      return;
    }
    classes[className] = { seats: seats[className], cancellationRate: rate / 100 };
  }
  const racLimit = Number(data.get("racLimit"));
  const wlLimit = Number(data.get("wlLimit"));
  if (!/^\d{1,10}$/.test(number) || !name || stations.length < 2 || stations.length > 16 ||
      new Set(stations.map((station) => station.toLowerCase())).size !== stations.length ||
      !stations.every((station) => station.length <= 31) ||
      !Number.isInteger(racLimit) || racLimit < 0 || racLimit > 1000 ||
      !Number.isInteger(wlLimit) || wlLimit < 0 || wlLimit > 1000 ||
      !CLASS_NAMES.some((className) => seats[className] > 0)) {
    showToast("Check the train number, route, class capacity, and queue limits.", true);
    return;
  }
  const existing = findTrain(number);
  const activeForTrain = activeBookings().some((booking) => booking.trainNumber === number);
  if (activeForTrain) {
    showToast("A train with active bookings cannot be reconfigured.", true);
    return;
  }
  const train = { number, name, stations, classes, racLimit, wlLimit };
  if (existing) {
    state.trains = state.trains.map((item) => item.number === number ? train : item);
    addEvent(`Train ${number} configuration updated.`);
  } else {
    state.trains.push(train);
    addEvent(`Train ${number} added to the directory.`);
  }
  state.selectedTrain = null;
  persist();
  render();
  showToast(existing ? "Train configuration updated." : "Train added to the directory.");
}

function removeTrain(number) {
  if (activeBookings().some((booking) => booking.trainNumber === number)) {
    showToast("Cancel active bookings before removing this train.", true);
    return;
  }
  const train = findTrain(number);
  if (!train) return;
  if (!window.confirm(`Remove ${train.number} · ${train.name} from the directory?`)) return;
  state.trains = state.trains.filter((item) => item.number !== number);
  state.selectedTrain = null;
  addEvent(`Train ${number} removed from the directory.`);
  persist();
  render();
  showToast("Train removed.");
}

function undoLastAction() {
  const snapshot = undoStack.pop();
  if (!snapshot) {
    showToast("There is no booking or cancellation to undo.", true);
    return;
  }
  state.bookings = snapshot.bookings;
  state.nextPnr = snapshot.nextPnr;
  state.events = snapshot.events;
  addEvent("Last booking or cancellation was undone.");
  persist();
  render();
  showToast("Last booking or cancellation undone.");
}

function exportLog() {
  const lines = state.events.slice().reverse().map((event) =>
    `${new Date(event.time).toISOString()} ${event.message}`);
  const blob = new Blob([lines.join("\r\n") + "\r\n"], { type: "text/plain;charset=utf-8" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = "notifications.log";
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(link.href);
  showToast("Notification log downloaded.");
}

function resetDemo() {
  if (!window.confirm("Reset all RailEase browser demo data? This removes saved bookings and custom trains.")) return;
  state = freshState();
  undoStack.length = 0;
  persist();
  render();
  showToast("Demo data reset.");
}

document.addEventListener("click", (event) => {
  const pageLink = event.target.closest("[data-page-link]");
  if (pageLink) {
    event.preventDefault();
    setPage(pageLink.dataset.pageLink);
    return;
  }
  const selectTrain = event.target.closest("[data-select-train]");
  if (selectTrain) {
    state.selectedTrain = selectTrain.dataset.selectTrain;
    render();
    document.getElementById("passenger-form-panel")?.scrollIntoView({ behavior: "smooth", block: "start" });
    return;
  }
  const cancel = event.target.closest("[data-cancel-booking]");
  if (cancel) {
    const result = cancelBooking(cancel.dataset.cancelBooking);
    if (!result.ok) {
      showToast(result.message, true);
      return;
    }
    state.lastLookup = result.booking.pnr;
    saveUndoableChange(`Booking cancelled · estimated refund ${formatMoney(result.refund)}.`);
    return;
  }
  const edit = event.target.closest("[data-edit-train]");
  if (edit) {
    state.selectedTrain = edit.dataset.editTrain;
    render();
    document.getElementById("admin-number")?.focus();
    return;
  }
  const remove = event.target.closest("[data-remove-train]");
  if (remove) {
    removeTrain(remove.dataset.removeTrain);
    return;
  }
  if (event.target.closest("[data-clear-edit]")) {
    state.selectedTrain = null;
    render();
    return;
  }
  if (event.target.closest("[data-undo]")) {
    undoLastAction();
    return;
  }
  if (event.target.closest("[data-export-log]")) {
    exportLog();
    return;
  }
  if (event.target.closest("[data-reset-demo]")) resetDemo();
});

view.addEventListener("submit", (event) => {
  event.preventDefault();
  const form = event.target;
  if (form.id === "quick-search-form" || form.id === "train-search-form") {
    if (searchFromForm(form)) {
      state.page = "book";
      render();
    }
  } else if (form.id === "booking-form") {
    makeBooking(form);
  } else if (form.id === "pnr-search-form") {
    const data = new FormData(form);
    const pnr = String(data.get("pnr") || "").trim();
    const booking = state.bookings.find((item) => item.pnr === pnr);
    if (!/^\d{10}$/.test(pnr) || !booking) {
      state.lastLookup = "";
      render();
      showToast("No booking found for that 10-digit PNR.", true);
      return;
    }
    state.lastLookup = pnr;
    render();
  } else if (form.id === "train-admin-form") {
    updateTrain(form);
  } else if (form.id === "simulation-form") {
    simulateCancellations(form);
  }
});

view.addEventListener("change", (event) => {
  if (event.target.id !== "sim-train") return;
  const train = findTrain(event.target.value);
  const classSelect = document.getElementById("sim-class");
  if (!train || !classSelect) return;
  const previous = classSelect.value;
  classSelect.innerHTML = classOptions(previous, train);
  if (!enabledClasses(train).includes(previous)) {
    classSelect.value = enabledClasses(train)[0] || "";
  }
});

document.getElementById("today-label").textContent = todayLabel();
render();
if (storageWarning) showToast(storageWarning, true);
