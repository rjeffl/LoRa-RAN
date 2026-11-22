// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// custom:lran-gate-schedule-card, the list and the Gate Event dialog on Gate Schedule (HA
// Integration Implementation Plan 8.5). People who are not technical use it, so it shows the
// action, the day and time and an enabled switch, and nothing else (operator, 2026-11-21).
//
// The Scheduler component (nielsfaber) stores the events and fires them. This card replaces
// the Scheduler card only, because that card has no setting for its dialog's title, defaults
// or action buttons, and cannot hide its gear and scheme views. It talks to the component
// through the component's own services and websocket commands, read from v3.3.9's source.
//
// What an event does when it fires is in packages/lran/schedule.yaml (plan 9.6), not here.

const kScripts = {
  open: "script.lran_gate_schedule_open_hold",
  close: "script.lran_gate_schedule_close",
};
const kLabels = { open: "Open", close: "Close" };
const kMasterSwitch = "input_boolean.lran_gate_schedule_enabled";
const kDefaultTime = "09:00";
// About ten rows before the list scrolls (operator, 2026-11-21).
const kListMaxHeight = "560px";

// The date and time as HA's own time zone has them, so a phone in another zone gets the
// same defaults and checks as the server that fires the event.
function zoned_parts(date, time_zone) {
  const parts = {};
  for (const p of new Intl.DateTimeFormat("en-CA", {
    timeZone: time_zone,
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    hourCycle: "h23",
  }).formatToParts(date)) {
    parts[p.type] = p.value;
  }
  return {
    date: `${parts.year}-${parts.month}-${parts.day}`,
    time: `${parts.hour}:${parts.minute}`,
  };
}

function add_days(iso_date, days) {
  const d = new Date(`${iso_date}T12:00:00Z`);
  d.setUTCDate(d.getUTCDate() + days);
  return d.toISOString().slice(0, 10);
}

// A once event is a single run on one date: weekdays daily, the start and end dates both
// that day, and repeat_type single, so the component deletes it after it fires
// (switch.py, async_timer_updated).
function event_from_schedule(s) {
  const service = s.timeslots?.[0]?.actions?.[0]?.service;
  const action = Object.keys(kScripts).find((k) => kScripts[k] === service);
  if (!action) return null;
  const daily = s.repeat_type === "repeat" && !s.end_date;
  return {
    entity_id: s.entity_id,
    action,
    daily,
    date: s.start_date,
    time: (s.timeslots[0].start || "").slice(0, 5),
    enabled: s.enabled,
    next: s.timestamps?.[s.next_entries?.[0]] ?? null,
  };
}

class LranGateScheduleCard extends HTMLElement {
  setConfig(config) {
    this._config = config || {};
    this._events = [];
    this._dialog = null;
    if (!this.shadowRoot) this.attachShadow({ mode: "open" });
    this._render();
  }

  set hass(hass) {
    const first = !this._hass;
    const master_was = this._hass?.states[kMasterSwitch]?.state;
    this._hass = hass;
    if (first) {
      this._subscribe();
      this._refresh();
    } else if (hass.states[kMasterSwitch]?.state !== master_was) {
      this._background_render();
    }
  }

  getCardSize() {
    return 6;
  }

  getGridOptions() {
    return { columns: 12 };
  }

  connectedCallback() {
    // A once event in the past is waiting for the component to delete it; this hides it
    // at the minute it fires rather than at the next update.
    this._tick = setInterval(() => this._background_render(), 60 * 1000);
    if (this._hass && !this._unsub) {
      this._subscribe();
      this._refresh();
    }
  }

  disconnectedCallback() {
    clearInterval(this._tick);
    if (this._unsub) {
      this._unsub.then((unsub) => unsub()).catch(() => {});
      this._unsub = null;
    }
  }

  _subscribe() {
    if (this._unsub || !this._hass) return;
    this._unsub = this._hass.connection.subscribeMessage(
      () => this._refresh(),
      { type: "scheduler_updated" },
    );
    this._unsub.catch(() => {
      this._unsub = null;
    });
  }

  async _refresh() {
    try {
      const list = await this._hass.callWS({ type: "scheduler" });
      this._events = list.map(event_from_schedule).filter(Boolean);
      this._error = null;
    } catch (err) {
      this._error = "The schedule could not be loaded.";
    }
    this._background_render();
  }

  // A redraw rebuilds the dialog's inputs, which would take the focus from a field being
  // typed in, so an update that nobody asked for waits until the dialog closes.
  _background_render() {
    if (!this._dialog) this._render();
  }

  _now() {
    return zoned_parts(new Date(), this._hass.config.time_zone);
  }

  _visible_events() {
    const now = this._now();
    const now_key = `${now.date}T${now.time}`;
    return this._events
      .filter((e) => e.daily || (e.date && `${e.date}T${e.time}` >= now_key))
      .sort((a, b) => {
        // Disabled events have no next run; they go by their own date and time instead.
        const key = (e) =>
          e.next ? Date.parse(e.next) : e.date ? Date.parse(`${e.date}T${e.time}:00`) : Number.MAX_SAFE_INTEGER;
        return key(a) - key(b);
      });
  }

  _when(e) {
    const tz = this._hass.config.time_zone;
    const lang = this._hass.locale?.language || "en-US";
    if (!e.next && !e.date) {
      // A daily event made before this card has no start date, and off it has no next run.
      const t = new Date(`2000-01-01T${e.time}:00`);
      return `Daily, ${t.toLocaleTimeString(lang, { hour: "numeric", minute: "2-digit" })}`;
    }
    const at = e.next ? new Date(e.next) : new Date(`${e.date}T${e.time}:00`);
    const opts = e.next ? { timeZone: tz } : {};
    const day = at.toLocaleDateString(lang, {
      ...opts,
      weekday: "short",
      month: "short",
      day: "numeric",
    });
    const time = at.toLocaleTimeString(lang, { ...opts, hour: "numeric", minute: "2-digit" });
    return e.daily ? `Daily · next ${day}, ${time}` : `${day}, ${time}`;
  }

  _render() {
    if (!this.shadowRoot) return;
    const master_off = this._hass?.states[kMasterSwitch]?.state === "off";
    const events = this._hass ? this._visible_events() : [];
    const rows = events
      .map(
        (e, i) => `
        <div class="row" data-i="${i}" role="button" tabindex="0">
          <div class="event">
            <div class="action">${kLabels[e.action]}</div>
            <div class="when">${this._when(e)}</div>
          </div>
          <ha-switch data-i="${i}" ${e.enabled ? "checked" : ""}></ha-switch>
        </div>`,
      )
      .join("");

    this.shadowRoot.innerHTML = `
      <style>${kStyle}</style>
      <ha-card>
        <div class="header">
          <div class="title">Scheduled Gate Events</div>
          <button class="add">+ Add</button>
        </div>
        ${master_off ? `<div class="notice">Scheduled events are turned off. They will not move the gate.</div>` : ""}
        ${this._error ? `<div class="notice">${this._error}</div>` : ""}
        <div class="columns"><span>Event</span><span>Enabled</span></div>
        <div class="list" style="max-height:${kListMaxHeight}">
          ${rows || `<div class="empty">No events scheduled.</div>`}
        </div>
      </ha-card>
      ${this._dialog ? this._dialog_html() : ""}`;

    this.shadowRoot.querySelector(".add").addEventListener("click", () => this._open_dialog(null));
    this.shadowRoot.querySelectorAll(".row").forEach((row) => {
      const open = () => this._open_dialog(events[Number(row.dataset.i)]);
      row.addEventListener("click", open);
      row.addEventListener("keydown", (ev) => {
        if (ev.key === "Enter" || ev.key === " ") open();
      });
    });
    this.shadowRoot.querySelectorAll("ha-switch").forEach((sw) => {
      // The switch is inside the row, so stop its tap from opening the dialog too.
      sw.addEventListener("click", (ev) => ev.stopPropagation());
      sw.addEventListener("change", (ev) => {
        const e = events[Number(sw.dataset.i)];
        this._hass.callService("switch", ev.target.checked ? "turn_on" : "turn_off", {
          entity_id: e.entity_id,
        });
      });
    });
    if (this._dialog) this._wire_dialog();
  }

  _open_dialog(e) {
    const now = this._now();
    this._dialog = e
      ? { entity_id: e.entity_id, action: e.action, daily: e.daily, date: e.date || add_days(now.date, 1), time: e.time }
      : { entity_id: null, action: null, daily: false, date: add_days(now.date, 1), time: kDefaultTime };
    this._dialog.confirm_delete = false;
    this._dialog.message = "";
    this._render();
  }

  _dialog_html() {
    const d = this._dialog;
    const seg = (name, value, label, on) =>
      `<button class="seg ${on ? "on" : ""}" data-${name}="${value}" aria-pressed="${on}">${label}</button>`;
    const footer = d.confirm_delete
      ? `<span class="ask">Delete this event?</span>
         <button class="text" data-act="keep">No</button>
         <button class="danger" data-act="really-delete">Delete</button>`
      : `${d.entity_id ? `<button class="text danger-text" data-act="delete">Delete</button>` : ""}
         <span class="spacer"></span>
         <button class="text" data-act="cancel">Cancel</button>
         <button class="primary" data-act="save" ${d.action ? "" : "disabled"}>Save</button>`;
    return `
      <div class="scrim">
        <div class="dialog" role="dialog" aria-label="Gate Event">
          <div class="dtitle">Gate Event</div>
          <label>Day<input type="date" name="date" value="${d.date}" min="${this._now().date}"></label>
          <div class="field">Time${time_selects(d.time)}</div>
          <div class="field">Repeat
            <div class="segs">${seg("repeat", "once", "Once", !d.daily)}${seg("repeat", "daily", "Daily", d.daily)}</div>
          </div>
          <div class="field">Gate action
            <div class="segs">${seg("action", "open", "Open", d.action === "open")}${seg("action", "close", "Close", d.action === "close")}</div>
          </div>
          <div class="message">${d.message}</div>
          <div class="footer">${footer}</div>
        </div>
      </div>`;
  }

  _wire_dialog() {
    const root = this.shadowRoot;
    const d = this._dialog;
    // Keep what was typed when a button press redraws the dialog.
    root.querySelector('input[name="date"]').addEventListener("change", (ev) => (d.date = ev.target.value));
    root.querySelectorAll("select[data-time]").forEach((sel) =>
      sel.addEventListener("change", () => (d.time = time_from_selects(root))),
    );
    root.querySelectorAll("[data-repeat]").forEach((b) =>
      b.addEventListener("click", () => {
        d.daily = b.dataset.repeat === "daily";
        this._render();
      }),
    );
    root.querySelectorAll("[data-action]").forEach((b) =>
      b.addEventListener("click", () => {
        d.action = b.dataset.action;
        this._render();
      }),
    );
    root.querySelector(".scrim").addEventListener("click", (ev) => {
      if (ev.target.classList.contains("scrim")) this._close_dialog();
    });
    root.querySelectorAll("[data-act]").forEach((b) =>
      b.addEventListener("click", () => this._dialog_act(b.dataset.act)),
    );
  }

  _close_dialog() {
    this._dialog = null;
    this._render();
  }

  async _dialog_act(act) {
    const d = this._dialog;
    if (act === "cancel") return this._close_dialog();
    if (act === "delete" || act === "keep") {
      d.confirm_delete = act === "delete";
      return this._render();
    }
    try {
      if (act === "really-delete") {
        await this._hass.callService("scheduler", "remove", { entity_id: d.entity_id });
        return this._close_dialog();
      }
      if (act === "save") {
        if (!d.date || !d.time) {
          d.message = "Pick a day and a time.";
          return this._render();
        }
        const now = this._now();
        if (!d.daily && `${d.date}T${d.time}` <= `${now.date}T${now.time}`) {
          d.message = "That time has already passed. Pick a later one.";
          return this._render();
        }
        const data = {
          weekdays: ["daily"],
          start_date: d.date,
          end_date: d.daily ? null : d.date,
          timeslots: [{ start: `${d.time}:00`, actions: [{ service: kScripts[d.action] }] }],
          repeat_type: d.daily ? "repeat" : "single",
        };
        if (d.entity_id) {
          await this._hass.callService("scheduler", "edit", { entity_id: d.entity_id, ...data });
        } else {
          await this._hass.callService("scheduler", "add", { ...data, name: `Gate ${kLabels[d.action]}` });
        }
        return this._close_dialog();
      }
    } catch (err) {
      d.message = "That did not save. Try again.";
      this._render();
    }
  }
}

const kStyle = `
  ha-card { padding: 16px; }
  .header { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
  .title { font-size: 1.25rem; font-weight: 500; color: var(--primary-text-color); }
  button { font: inherit; cursor: pointer; border-radius: 8px; }
  .add { border: none; background: var(--primary-color); color: var(--text-primary-color, #fff);
         padding: 8px 16px; font-weight: 500; }
  .notice { margin-top: 12px; padding: 8px 12px; border-radius: 8px; color: var(--primary-text-color);
            background: rgba(255, 140, 0, 0.18); }
  .columns { display: flex; justify-content: space-between; margin-top: 16px; padding: 0 4px 6px;
             font-size: 0.85rem; color: var(--secondary-text-color); border-bottom: 1px solid var(--divider-color); }
  .list { overflow-y: auto; }
  .row { display: flex; align-items: center; justify-content: space-between; gap: 12px;
         padding: 10px 4px; border-bottom: 1px solid var(--divider-color); cursor: pointer; }
  .action { font-weight: 500; color: var(--primary-text-color); }
  .when { color: var(--secondary-text-color); font-size: 0.95rem; }
  .empty { padding: 16px 4px; color: var(--secondary-text-color); }
  .scrim { position: fixed; inset: 0; z-index: 10; background: rgba(0, 0, 0, 0.45);
           display: flex; align-items: center; justify-content: center; padding: 16px; }
  .dialog { width: 100%; max-width: 380px; box-sizing: border-box; padding: 20px; border-radius: 16px;
            background: var(--card-background-color, #fff); color: var(--primary-text-color);
            display: flex; flex-direction: column; gap: 14px; }
  .dtitle { font-size: 1.3rem; font-weight: 500; }
  label, .field { display: flex; flex-direction: column; gap: 6px; font-size: 0.9rem; color: var(--secondary-text-color); }
  input, select { font: inherit; font-size: 1rem; padding: 10px; border-radius: 8px; color: var(--primary-text-color);
          background: var(--secondary-background-color, transparent); border: 1px solid var(--divider-color); }
  .time { display: flex; align-items: center; gap: 6px; color: var(--primary-text-color); }
  .time select { flex: 1; }
  .segs { display: flex; gap: 8px; }
  .seg { flex: 1; padding: 12px; font-size: 1rem; background: transparent; color: var(--primary-text-color);
         border: 1px solid var(--divider-color); }
  .seg.on { background: var(--primary-color); color: var(--text-primary-color, #fff); border-color: var(--primary-color); }
  .message { min-height: 1.2em; color: var(--error-color, #db4437); font-size: 0.9rem; }
  .footer { display: flex; align-items: center; gap: 8px; }
  .spacer, .ask { flex: 1; }
  .text { border: none; background: transparent; color: var(--primary-color); padding: 8px 12px; }
  .danger-text { color: var(--error-color, #db4437); }
  .primary { border: none; background: var(--primary-color); color: var(--text-primary-color, #fff); padding: 8px 18px; }
  .primary[disabled] { opacity: 0.4; cursor: default; }
  .danger { border: none; background: var(--error-color, #db4437); color: #fff; padding: 8px 18px; }
`;

// Time is three selects, hour, minute and AM/PM, not <input type="time">. In the Mac Companion
// app (2026.10.3, a Catalyst build) that input opens UIKit's UIPickerView, which Catalyst's Mac
// idiom refuses with an exception, and the app quits (crash reports, 2026-11-21). A select opens
// the wheel on the iPhone, which the operator wants kept, and a menu on the Mac.
function time_selects(hhmm) {
  const [h24, min] = hhmm.split(":").map(Number);
  const h12 = h24 % 12 || 12;
  const opt = (value, label, on) => `<option value="${value}" ${on ? "selected" : ""}>${label}</option>`;
  const hours = Array.from({ length: 12 }, (_, i) => opt(i + 1, i + 1, i + 1 === h12)).join("");
  const mins = Array.from({ length: 60 }, (_, i) => opt(i, String(i).padStart(2, "0"), i === min)).join("");
  const ampm = opt("am", "AM", h24 < 12) + opt("pm", "PM", h24 >= 12);
  return `<div class="time">
      <select data-time="hour" aria-label="Hour">${hours}</select>
      <span>:</span>
      <select data-time="minute" aria-label="Minute">${mins}</select>
      <select data-time="ampm" aria-label="AM or PM">${ampm}</select>
    </div>`;
}

// The Scheduler component takes 24-hour HH:MM, so the dialog keeps d.time in that form.
function time_from_selects(root) {
  const get = (name) => root.querySelector(`select[data-time="${name}"]`).value;
  const h12 = Number(get("hour")) % 12;
  const h24 = get("ampm") === "pm" ? h12 + 12 : h12;
  return `${String(h24).padStart(2, "0")}:${String(Number(get("minute"))).padStart(2, "0")}`;
}

customElements.define("lran-gate-schedule-card", LranGateScheduleCard);
window.customCards = window.customCards || [];
window.customCards.push({
  type: "lran-gate-schedule-card",
  name: "LRAN gate schedule",
  description: "Scheduled gate events for Gate Schedule (HA plan 8.5).",
});
