import { defineConfig } from 'vite';
import { resolve } from 'node:path';

const snapshot = process.env.TWEEBBO_SNAPSHOT;
if (!snapshot) throw new Error('TWEEBBO_SNAPSHOT is not set');

export default defineConfig({
  logLevel: 'warn',
  publicDir: false,
  resolve: { alias: { '@web': resolve(snapshot, 'src') } },
  build: {
    outDir: resolve(process.env.TWEEBBO_EXPORT_BUILD ?? '.build'),
    emptyOutDir: true,
    target: 'es2022',
    rollupOptions: {
      input: { export: resolve(import.meta.dirname, 'src/export.ts') },
      output: { entryFileNames: '[name].js', format: 'es' },
    },
  },
});
