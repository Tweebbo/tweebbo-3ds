// Download QR code (FBI → Remote Install → Scan QR code): node qr.mjs <url> <output.png>
import QRCode from 'qrcode';
const [url, out, scale = '8'] = process.argv.slice(2);
await QRCode.toFile(out, url, { errorCorrectionLevel: 'M', margin: 2, scale: Number(scale), color: { dark: '#111920', light: '#ffffff' } });
console.log(`QR: ${out} → ${url}`);
