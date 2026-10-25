// Maintainer-only SVG rasterization. Install sharp@0.35.5 beside this script
// or expose an existing installation with NODE_PATH. No font or network lookup.
const path = require('node:path');
const sharp = require('sharp');

sharp(path.join(__dirname, '..', 'assets', 'branding', 'poseanchor-icon.svg'), {
  density: 144,
}).resize(1024, 1024).png().toBuffer()
  .then(buffer => process.stdout.write(buffer))
  .catch(error => { console.error(error.message); process.exitCode = 1; });
