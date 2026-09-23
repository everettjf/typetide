# Website release checks

macOS and Windows share release tags but may upload their assets at different times.
The newest release is not necessarily a download source for both platforms.

Before updating `docs/index.html`:

1. Read the public, non-draft, non-prerelease GitHub releases and find the newest
   uploaded DMG and Windows installer separately. Verify their asset URLs.
2. Update both pairs of download buttons and the platform-version line together.
   Keep the all-releases link available for portable ZIPs and older versions.
3. Verify that the Homebrew cask points to an existing Mac asset and matching hash.
4. Base feature claims on the relevant released build. Built-in MLX requires Apple
   Silicon, a one-time model download, and Faithful style; Intel and Windows use
   Ollama or an API. Do not imply the Windows download contains the Mac backend.
5. Keep English and Chinese READMEs, introduction pages, and the xnu.app catalog
   aligned. Use the lowercase canonical URL `https://xnu.app/typetide/`.
6. Check desktop and narrow layouts, local image paths, download-link targets,
   and the live page after GitHub Pages reports a successful deployment.

The platform-specific links intentionally point to verified release assets rather
than the shared `releases/latest` endpoint. Update them as each platform ships.
