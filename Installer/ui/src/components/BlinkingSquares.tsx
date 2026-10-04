import { useEffect, useRef } from 'react';

// TEMPORARY STAND-IN — DEVIATION (documented in the phase report):
// The locked spec requires the exact registry component
// `@reactbits-starter/blinking-squares-tw` (<BlinkingSquares />), but that
// namespace does not resolve on the public shadcn registry index
// ("Unknown registry", verified 2026-09-27; the `@react-bits` directory
// registry carries no such item; the Pro registry needs a license key we
// do not possess). This local wrapper keeps the EXACT <BlinkingSquares />
// contract (no props) so swapping in the licensed package later is a
// one-file change, and renders a canvas square-grid ambient visual in the
// same spirit (right-side/background only, never full-screen dominant).
// DO NOT treat this file as the final artwork.

export default function BlinkingSquares() {
  const ref = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    let raf = 0;
    let w = 0;
    let h = 0;
    const resize = () => {
      const r = canvas.getBoundingClientRect();
      w = canvas.width = Math.max(1, Math.floor(r.width));
      h = canvas.height = Math.max(1, Math.floor(r.height));
    };
    resize();
    window.addEventListener('resize', resize);
    const cell = 14;
    const gap = 5;
    // Deterministic pseudo-random per cell (stable shimmer, no GC churn).
    const hash = (x: number, y: number) => {
      let n = (x * 374761393 + y * 668265263) | 0;
      n = (n ^ (n >> 13)) | 0;
      n = (n * 1274126177) | 0;
      return ((n ^ (n >> 16)) >>> 0) / 4294967295;
    };
    const draw = (t: number) => {
      ctx.clearRect(0, 0, w, h);
      // Density fades leftward so content stays dominant.
      for (let y = 0; y * (cell + gap) < h; y += 1) {
        for (let x = 0; x * (cell + gap) < w; x += 1) {
          const px = x * (cell + gap);
          const density = 0.15 + 0.85 * (px / Math.max(1, w));
          const r = hash(x, y);
          if (r > density) continue;
          const blink = 0.5 + 0.5 * Math.sin(t / 900 + r * 40);
          const a = 0.08 + 0.55 * blink * density;
          const bright = r > 0.93;
          ctx.fillStyle = bright
            ? `rgba(120, 200, 255, ${Math.min(1, a + 0.3)})`
            : `rgba(47, 128, 237, ${a})`;
          ctx.fillRect(px, y * (cell + gap), cell, cell);
        }
      }
      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => {
      cancelAnimationFrame(raf);
      window.removeEventListener('resize', resize);
    };
  }, []);

  return (
    <canvas
      ref={ref}
      aria-hidden
      className="pointer-events-none absolute inset-0 h-full w-full"
    />
  );
}
