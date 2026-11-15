#!/usr/bin/env node
// Hardware-in-the-loop runner. Runs the e2e scenario table against:
//   --board URL     the ESP32-S3 on the bench (bench mode only), or
//   --simulated     airo-host --adapter esp32 (the board's adapter on the laptop)
//
//   node scripts/hil.mjs --list
//   node scripts/hil.mjs --board http://airo-desk-001.local --token <token>
//   node scripts/hil.mjs --board http://192.168.1.50 --token <t> --only physical-keys,app-lease --case A2
//   node scripts/hil.mjs --simulated --only keypad-wins
//
// Options: --only id,id  --case A1,A2,B  --slack ms (extra timing budget for
// Wi-Fi, default 150)  --allow-drive (run even with bench drive on).
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const opt = (name) => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};
const has = (name) => args.includes(name);

if (has('--help') || has('-h') || args.length === 0) {
  console.log(
    'usage: node scripts/hil.mjs (--board URL --token T | --simulated | --list) [--only ids] [--case A1,A2,B] [--slack ms] [--allow-drive]',
  );
  process.exit(args.length === 0 ? 2 : 0);
}

const env = { ...process.env };
if (opt('--only')) env.AIRO_SCENARIOS = opt('--only');
if (opt('--case')) env.AIRO_CASES = opt('--case');
if (opt('--slack')) env.AIRO_HIL_SLACK_MS = opt('--slack');
if (has('--allow-drive')) env.AIRO_HIL_ALLOW_DRIVE = '1';

const run = (cmd, a) => spawnSync(cmd, a, { stdio: 'inherit', env, shell: process.platform === 'win32' }).status ?? 1;

if (has('--list')) {
  // Read the table's ids straight from e2e/scenarios.ts (no TS runtime needed).
  const src = readFileSync(new URL('../e2e/scenarios.ts', import.meta.url), 'utf8');
  const re = /id: '([^']+)',\s*row: '([^']+)',\s*title: '([^']+)',\s*cases: ([^,\n]+),/g;
  console.log('scenario id               cases       spec 12.1 row');
  for (const m of src.matchAll(re)) {
    const cases = m[4] === 'CASES' ? 'A1,A2,B' : m[4].replace(/[[\]' ]/g, '');
    console.log(`${m[1].padEnd(25)} ${cases.padEnd(11)} ${m[2]}`);
  }
  process.exit(0);
}

if (has('--simulated')) {
  if (run('npm', ['run', 'firmware:build', '--silent']) !== 0) process.exit(1);
  process.exit(run('npx', ['vitest', 'run', '-c', 'vitest.e2e.config.ts', 'e2e/esp32-adapter.e2e.test.ts']));
}

const board = opt('--board');
const token = opt('--token') ?? process.env.AIRO_TOKEN;
if (!board || !token) {
  console.error('--board URL and --token T (or AIRO_TOKEN) are required. The token is printed by "token" on the serial console.');
  process.exit(2);
}
env.AIRO_BOARD_URL = board;
env.AIRO_TOKEN = token;
process.exit(run('npx', ['vitest', 'run', '-c', 'vitest.hil.config.ts']));
