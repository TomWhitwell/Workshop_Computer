// Terminal presentation shared by the pre-commit hook processes.
//
// Git runs hooks with stdout redirected to stderr. In a terminal that stream is
// a TTY, so we show live progress and put the verdict last, where it is easiest
// to see. GUI clients (VS Code, etc.) capture the stream instead; VS Code shows
// only the first non-empty line in its error dialog, so there the verdict must
// come first and progress is omitted.

export const interactive = Boolean(process.stdout.isTTY);
const colorEnabled = interactive && !process.env.NO_COLOR;

const paint = code => text => colorEnabled ? `\x1b[${code}m${text}\x1b[0m` : String(text);
export const color = {
  red: paint('31'),
  yellow: paint('33'),
  green: paint('32'),
  dim: paint('2'),
  bold: paint('1'),
};

/** Start a progress step. Prints nothing outside a terminal. */
export function step(label) {
  if (!interactive) return { done() {}, fail() {} };
  process.stdout.write(`  ${color.dim('…')} ${label}`);
  let finished = false;
  const finish = (mark, detail) => {
    if (finished) return;
    finished = true;
    process.stdout.write(`\r\x1b[K  ${mark} ${label}${detail ? color.dim(` — ${detail}`) : ''}\n`);
  };
  return {
    done: detail => finish(color.green('✓'), detail),
    fail: detail => finish(color.red('✗'), detail),
  };
}

/** Print the verdict first for GUI clients, or last in a terminal. */
export function printReport({ headline, body = [], footer = [] }) {
  const lines = interactive
    ? [...(body.length ? ['', ...body] : []), '', headline, ...footer]
    : [headline, ...(body.length ? ['', ...body] : []), ...(footer.length ? ['', ...footer] : [])];
  console.log(lines.join('\n'));
}
