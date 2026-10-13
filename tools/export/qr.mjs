// Download QR code (FBI → Remote Install → Scan QR code): node qr.mjs <url> <output.png>
import QRCode from 'qrcode';
const [url, out] = process.argv.slice(2);
await QRCode.toFile(out, url, { errorCorrectionLevel: 'M', margin: 2, scale: 8, color: { dark: '#111920', light: '#ffffff' } });
console.log(`QR: ${out} → ${url}`);
