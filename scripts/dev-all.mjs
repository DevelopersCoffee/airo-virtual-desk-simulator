// Builds and starts airo-host (the C++ Airo Core) and the Vite dashboard together.
import { spawn, spawnSync } from 'node:child_process';

const build = spawnSync('npm', ['run', 'firmware:build'], { stdio: 'inherit' });
if (build.status !== 0) process.exit(build.status ?? 1);

const children = [
  spawn('./firmware/build/airo-host', ['--port', '8080'], { stdio: 'inherit' }),
  spawn('npx', ['vite'], { stdio: 'inherit' }),
];
const stop = () => {
  for (const c of children) c.kill('SIGTERM');
  process.exit(0);
};
process.on('SIGINT', stop);
process.on('SIGTERM', stop);
for (const c of children) c.on('exit', stop);
