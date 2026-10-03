# Lunar Player Website

Official product website for **Lunar Player by SSK** — *"Simple by default. Powerful when you need it."*

- **Live site:** https://ssk-animator.github.io/LunarPlayer/
- **Source:** this directory (`/Website` in the LunarPlayer repository)
- **Deployment:** `.github/workflows/deploy-website.yml` (GitHub Actions → GitHub Pages,
  triggers on pushes to `master` that touch `Website/**`)

## Contents

Pure static site, no build step and no dependencies:

```text
Website/
├── index.html   # all sections: hero, features, comparison, demos, gallery, CTA, footer
├── styles.css   # dark cinematic design system + responsive breakpoints
├── app.js       # navigation, timeline/frame/subtitle/audio/export demos, lightbox
└── assets/
    └── LunarPlayer.png  # real Lunar Player logo (copied from the app's assets/)
```

## Preview locally

Serve this directory with any static server, e.g. `python3 -m http.server`, then open
`http://localhost:8000/index.html`.

## Notes

- All asset references are relative, so the site works both locally and under the
  `/LunarPlayer/` GitHub Pages subpath.
- Interactive panels (Subtitle Studio, Export, frame navigation) are UI demonstrations
  only — they simulate the product; no media is processed in the browser.
- Branding uses the real `LunarPlayer.png` logo from the desktop app. Do not replace it
  with generated artwork.
