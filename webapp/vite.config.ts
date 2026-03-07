import { defineConfig } from 'vite';

export default defineConfig({
    base: './',
    server: {
        port: 3000,
        headers: {
            "Cross-Origin-Opener-Policy": "same-origin",
            "Cross-Origin-Embedder-Policy": "require-corp",
        },
        proxy: {
            '/api/neon': {
                target: 'https://ep-muddy-fire-ads9qx8f.c-2.us-east-1.aws.neon.tech',
                changeOrigin: true,
                rewrite: (path) => path.replace(/^\/api\/neon/, '/sql')
            }
        }
    },
    optimizeDeps: {
        exclude: ['mujoco-js']
    }
});
