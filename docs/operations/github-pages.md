# SpiffOS GitHub Pages deployment

This repository contains a static public-facing project website under `site/`. The GitHub Actions workflow `.github/workflows/pages.yml` publishes that directory when `site/**` or the workflow changes on `main`, and can also be run manually.

## One-time GitHub repository setup (requires owner/admin permissions)

1. Open **Settings → Pages** in `spifford3472/SpiffOS`.
2. Under **Build and deployment**, choose **Source: GitHub Actions**.
3. Ensure **Actions** are enabled and the `github-pages` environment is allowed to deploy.
4. Check **Actions → Deploy SpiffOS website to GitHub Pages** for a successful run. If the workflow did not run automatically, use **Run workflow** on `main`.
5. GitHub should report the actual site URL in the deployment environment. For a normal user-owned project site it is expected to be `https://spifford3472.github.io/SpiffOS/`, but **do not claim it is live until GitHub reports a successful deployment and the URL loads**.

## Repository visibility and publication

GitHub Pages websites can be publicly accessible even when the source repository is private, depending on account/organization and Pages availability. Confirm the repository visibility, Pages eligibility and whether the project documentation is intended to be public **before enabling**. Publishing a site is distinct from publishing the repository itself.

## Content updates

- Edit `site/index.html` and `site/assets/spiffos-logo.svg` via a reviewed PR.
- Merge to `main` to trigger a new Pages deployment.
- The website currently links to some repository documents. If the repository is private, anonymous visitors cannot read those links; migrate selected approved manuals into the static site before advertising them as publicly accessible.
- No external CDN or tracking scripts are required. The site is a self-contained static HTML/CSS page with a local SVG asset.
