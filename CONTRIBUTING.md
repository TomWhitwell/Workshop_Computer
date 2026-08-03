# Contributing

Thanks for helping improve Workshop Computer.

This guide covers contributions to **this repository’s shared tooling, documentation, and metadata site** — not publishing a new program card.

## What this covers

- The metadata site generator (`tools/sitegen/`) and public catalogue at [computer.musicthing.co.uk](https://computer.musicthing.co.uk/)
- Docs under `documentation/`
- CI and GitHub Actions under `.github/`
- DevContainer, build scripts, and other shared developer tooling

## What this does not cover

Adding or updating a **program card** under `releases/` (firmware, `info.yaml`, leaflets, and so on) is a separate contribution path. See the [README](README.md) for the current releases/PR workflow.

## Ways to help

- Fix bugs or improve the metadata site, validators, or curation tooling
- Improve documentation clarity and accuracy
- Tighten CI, hooks, or DevContainer setup
- Report issues using the **Bug report** or **Feature request** templates

## Getting started

1. Fork the repository and create a branch for your change.
2. Make a focused change and open a pull request that explains **why** the change is needed.

You can work on your host machine, or use the **Visual Studio Code DevContainer**, which sets up a ready-made Linux environment with the tools this repo expects (including Node for the metadata site, and the Pico SDK stack for firmware). See [.devcontainer/README.md](.devcontainer/README.md).

### Metadata site

```sh
npm install --prefix tools/sitegen
npm run build
npm run dev
```

Details: [documentation/metadata-site-development.md](documentation/metadata-site-development.md).

Useful scripts from the repo root:

```sh
npm run validate-info
npm run check-curation
npm run test
npm run hooks:install
```

## Pull request expectations

- Keep changes focused and easy to review
- Prefer linking or extending existing docs over duplicating them
- Avoid committing secrets, credentials, or unrelated generated noise
- Follow the [Code of Conduct](CODE_OF_CONDUCT.md)

## Security

Please report vulnerabilities privately — see [SECURITY.md](SECURITY.md).
