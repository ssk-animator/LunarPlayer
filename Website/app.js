(() => {
"use strict";
const $ = (s, c = document) => c.querySelector(s);
const $$ = (s, c = document) => [...c.querySelectorAll(s)];
const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;

/* toast */
const toast = $("#toast");
let toastT;
function showToast(msg) {
  toast.textContent = msg;
  toast.hidden = false;
  clearTimeout(toastT);
  toastT = setTimeout(() => (toast.hidden = true), 3200);
}

/* nav: scrolled state + mobile menu */
const nav = $("#topnav");
addEventListener("scroll", () => nav.classList.toggle("scrolled", scrollY > 12), { passive: true });
nav.classList.toggle("scrolled", scrollY > 12);
const toggle = $("#navToggle"), mobile = $("#mobileMenu");
toggle.addEventListener("click", () => {
  const open = mobile.hidden;
  mobile.hidden = !open;
  toggle.setAttribute("aria-expanded", String(open));
  toggle.setAttribute("aria-label", open ? "Close menu" : "Open menu");
});
mobile.addEventListener("click", (e) => {
  if (e.target.tagName === "A") { mobile.hidden = true; toggle.setAttribute("aria-expanded", "false"); }
});
/* State hygiene: never leave the mobile menu logically open on desktop widths. */
matchMedia("(min-width: 861px)").addEventListener("change", (e) => {
  if (e.matches) { mobile.hidden = true; toggle.setAttribute("aria-expanded", "false"); }
});

/* scroll-spy */
const links = $$(".nav-link[data-spy]");
const secs = ["home", "features", "comparison", "screenshots", "download"].map((id) => document.getElementById(id)).filter(Boolean);
const spy = new IntersectionObserver((es) => {
  es.forEach((e) => {
    if (e.isIntersecting) {
      links.forEach((l) => l.classList.toggle("is-active", l.dataset.spy === e.target.id));
    }
  });
}, { rootMargin: "-40% 0px -55% 0px" });
secs.forEach((s) => spy.observe(s));

/* reveal on scroll */
const rev = new IntersectionObserver((es) => es.forEach((e) => {
  if (e.isIntersecting) { e.target.classList.add("in"); rev.unobserve(e.target); }
}), { threshold: 0.12 });
$$(".reveal").forEach((el) => rev.observe(el));

/* hero parallax */
const heroPlayer = $("#heroPlayer"), heroThumb = $("#heroThumb");
if (!reduced && heroPlayer) {
  const hero = $("#home");
  hero.addEventListener("mousemove", (e) => {
    const r = hero.getBoundingClientRect();
    const x = (e.clientX - r.left) / r.width - 0.5, y = (e.clientY - r.top) / r.height - 0.5;
    heroPlayer.style.transform = `rotateY(${x * 6}deg) rotateX(${-y * 5}deg)`;
  });
  hero.addEventListener("mouseleave", () => (heroPlayer.style.transform = ""));
}
$$("[data-demo-play]").forEach((b) => b.addEventListener("click", () => showToast("Demo only — open Lunar Player to play real media.")));

/* comparison tooltip (desktop) + accordion (mobile) */
const tip = $("#compareTip");
$$("#compareTable tbody tr").forEach((tr) => {
  tr.addEventListener("mousemove", (e) => {
    tip.textContent = tr.dataset.tip || "";
    tip.hidden = false;
    tip.style.left = Math.min(e.clientX + 16, innerWidth - 280) + "px";
    tip.style.top = e.clientY + 14 + "px";
  });
  tr.addEventListener("mouseleave", () => (tip.hidden = true));
  tr.addEventListener("focus", () => { tip.textContent = tr.dataset.tip || ""; tip.hidden = false; const r = tr.getBoundingClientRect(); tip.style.left = Math.min(r.left, innerWidth - 280) + "px"; tip.style.top = r.bottom + 8 + scrollY + "px"; });
  tr.tabIndex = 0;
});
const cards = $("#compareCards");
$$("#compareTable tbody tr").forEach((tr) => {
  const tds = [...tr.children].map((td) => td.textContent.trim());
  const d = document.createElement("details");
  d.innerHTML = `<summary>${tds[0]}</summary>
    <div class="cc-row"><span>VLC</span><strong>${tds[1]}</strong></div>
    <div class="cc-row"><span>Lunar Player</span><strong>${tds[2]}</strong></div>
    <p class="fine">${tr.dataset.tip || ""}</p>`;
  cards.appendChild(d);
});

/* timeline demo */
const track = $("#tlTrack"), fill = $("#tlFill"), knob = $("#tlKnob"), ts = $("#tlTs"), prev = $("#tlPreview");
const TOTAL = 2 * 3600 + 18 * 60 + 56; // 02:18:56
const fmt = (s) => [s / 3600, (s / 60) % 60, s % 60].map((n) => String(Math.floor(n)).padStart(2, "0")).join(":");
function setTl(p) {
  p = Math.max(0, Math.min(1, p));
  fill.style.width = p * 100 + "%"; knob.style.left = p * 100 + "%";
  track.setAttribute("aria-valuenow", Math.round(p * 100));
  ts.textContent = fmt(TOTAL * p);
  prev.style.background = `linear-gradient(${120 + p * 120}deg,#31456b,#5b3a86 ${20 + p * 60}%,#1a2540)`;
}
function tlFromEvent(e) {
  const r = track.getBoundingClientRect();
  const x = (e.touches ? e.touches[0].clientX : e.clientX) - r.left;
  setTl(x / r.width);
}
track.addEventListener("mousemove", tlFromEvent);
track.addEventListener("click", tlFromEvent);
track.addEventListener("keydown", (e) => {
  const cur = parseFloat(track.getAttribute("aria-valuenow")) / 100;
  if (e.key === "ArrowRight") setTl(cur + 0.02);
  if (e.key === "ArrowLeft") setTl(cur - 0.02);
});
setTl(0.38);

/* frame nav demo */
const fc = $("#frameCanvas"), fx = fc.getContext("2d");
let frame = 1042, playing = false, playT;
const FPS = 24;
function drawFrame(f) {
  const w = fc.width, h = fc.height;
  const g = fx.createLinearGradient(0, 0, w, h);
  g.addColorStop(0, "#1b2a4a"); g.addColorStop(1, "#3b2a5e");
  fx.fillStyle = g; fx.fillRect(0, 0, w, h);
  // "astronaut" dot moving per frame + mountain silhouette
  fx.fillStyle = "#0d1528";
  fx.beginPath(); fx.moveTo(0, h);
  for (let x = 0; x <= w; x += 20) fx.lineTo(x, h - 40 - Math.abs(Math.sin(x / 60 + f / 12)) * 60);
  fx.lineTo(w, h); fx.fill();
  const x = (f * 7) % w;
  fx.fillStyle = "#cfe1ff"; fx.beginPath(); fx.arc(x, 90 + Math.sin(f / 6) * 18, 16, 0, 7); fx.fill();
  fx.fillStyle = "#0b1220"; fx.fillRect(x - 8, 82 + Math.sin(f / 6) * 18, 16, 10);
  fx.fillStyle = "rgba(255,255,255,.75)"; fx.font = "12px monospace";
  fx.fillText("FRAME " + f, 12, 20);
  $("#frameLabel").textContent = `frame ${f} · ${fmt(f / FPS)} · 24 fps`;
}
function step(d) { frame = Math.max(0, frame + d); drawFrame(frame); }
$("#framePrev").addEventListener("click", () => { stopPlay(); step(-1); });
$("#frameNext").addEventListener("click", () => { stopPlay(); step(1); });
function stopPlay() { playing = false; clearInterval(playT); $("#framePlay").textContent = "▶ Play"; }
$("#framePlay").addEventListener("click", () => {
  if (playing) return stopPlay();
  playing = true; $("#framePlay").textContent = "⏸ Pause";
  playT = setInterval(() => { frame++; drawFrame(frame); }, reduced ? 500 : 1000 / FPS);
});
drawFrame(frame);

/* waveform */
const wc = $("#waveCanvas"), wx = wc.getContext("2d");
function drawWave(vol) {
  const w = wc.width, h = wc.height;
  wx.clearRect(0, 0, w, h);
  wx.fillStyle = "#070c17"; wx.fillRect(0, 0, w, h);
  const mid = h / 2;
  for (let x = 0; x < w; x += 3) {
    const a = Math.abs(Math.sin(x / 14) * Math.sin(x / 47)) * (mid - 6) * (vol / 100);
    const grad = wx.createLinearGradient(0, mid - a, 0, mid + a);
    grad.addColorStop(0, "#38e1c6"); grad.addColorStop(1, "#8b5cf6");
    wx.fillStyle = grad;
    wx.fillRect(x, mid - a, 2, a * 2);
  }
}
drawWave(100);
const bind = (id, out, fmtFn, cb) => {
  const el = $(id), o = $(out);
  el.addEventListener("input", () => { o.textContent = fmtFn(el.value); cb && cb(+el.value); });
};
bind("#volSlider", "#volOut", (v) => v + "%", drawWave);
bind("#balSlider", "#balOut", (v) => (v > 0 ? "R" + v : v < 0 ? "L" + -v : "0"));
bind("#delaySlider", "#delayOut", (v) => v + " ms");
$("#audioTrack").addEventListener("change", (e) => showToast("Switched to " + e.target.value));

/* subtitle studio simulation */
const subSamples = [
  ["00:01:24", "We need to check…"],
  ["00:01:28", "Before we render…"],
  ["00:01:32", "Let's adjust the lighting."],
];
$("#subGen").addEventListener("click", () => {
  const btn = $("#subGen"), prog = $("#subProgress"), list = $("#subList");
  btn.disabled = true; btn.textContent = "Generating…";
  prog.hidden = false; list.innerHTML = "<li><span>…</span> Listening with " + $("#subModel").value + " (" + $("#subLang").value + ")…</li>";
  setTimeout(() => {
    prog.hidden = true; btn.disabled = false; btn.textContent = "Generate Subtitles";
    list.innerHTML = subSamples.map(([t, x]) => `<li><span>${t}</span> ${x}</li>`).join("");
    showToast("Demo subtitles generated — editing happens in Lunar Player.");
  }, reduced ? 200 : 1600);
});

/* export demo */
const expSummary = $("#expSummary");
function expUpdate() {
  $("#expBitrateOut").textContent = $("#expBitrate").value + " Mbps";
  expSummary.textContent = `${$("#expFormat").value} · ${$("#expVCodec").value} · ${$("#expRes").value} · ${$("#expBitrate").value} Mbps · ${$("#expAudio").value}`;
}
["#expFormat", "#expVCodec", "#expRes", "#expBitrate", "#expAudio"].forEach((s) => $(s).addEventListener("input", expUpdate));
expUpdate();
let expTimer;
$("#expStart").addEventListener("click", () => {
  const wrap = $("#expProgress"), bar = $("#expBar"), pct = $("#expPct");
  wrap.hidden = false; let p = 0;
  $("#expStart").disabled = true;
  clearInterval(expTimer);
  expTimer = setInterval(() => {
    p = Math.min(100, p + 4);
    bar.style.width = p + "%"; pct.textContent = p + "%";
    if (p >= 100) { clearInterval(expTimer); $("#expStart").disabled = false; showToast("Export complete (simulated) — " + $("#expFormat").value); }
  }, reduced ? 20 : 120);
});
$("#expCancel").addEventListener("click", () => {
  clearInterval(expTimer);
  $("#expProgress").hidden = true; $("#expBar").style.width = "0"; $("#expPct").textContent = "0%";
  $("#expStart").disabled = false; showToast("Export cancelled.");
});

/* gallery lightbox */
const shots = $$(".shot"), lb = $("#lightbox"), lbShot = $("#lbShot"), lbTitle = $("#lbTitle");
let lbIdx = 0;
function openLb(i) {
  lbIdx = (i + shots.length) % shots.length;
  lbShot.className = "shot-screen big " + shots[lbIdx].querySelector(".shot-screen").classList[1];
  lbTitle.textContent = shots[lbIdx].dataset.title + ` (${lbIdx + 1}/${shots.length})`;
  lb.hidden = false; $("#lbClose").focus();
}
shots.forEach((s, i) => s.addEventListener("click", () => openLb(i)));
$("#lbClose").addEventListener("click", () => (lb.hidden = true));
$("#lbPrev").addEventListener("click", (e) => { e.stopPropagation(); openLb(lbIdx - 1); });
$("#lbNext").addEventListener("click", (e) => { e.stopPropagation(); openLb(lbIdx + 1); });
lb.addEventListener("click", (e) => { if (e.target === lb) lb.hidden = true; });
addEventListener("keydown", (e) => {
  if (lb.hidden) return;
  if (e.key === "Escape") lb.hidden = true;
  if (e.key === "ArrowRight") openLb(lbIdx + 1);
  if (e.key === "ArrowLeft") openLb(lbIdx - 1);
});

/* download placeholder — honest, no fake links */
$("#downloadBtn").addEventListener("click", () => {
  $("#downloadNote").textContent = "v0.1.0-alpha is in active development. Stable installers will be published here and on GitHub Releases.";
  showToast("No public installer yet — watch GitHub Releases for v0.1.0-alpha.");
});
})();
