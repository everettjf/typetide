# Built-in model delivery

The macOS model comes from the pinned Hugging Face revision in
`BuiltInModel.swift`. The app never downloads model code. A complete installation
is published only after every file matches its pinned size and digest.

Downloads use up to two concurrent 16 MiB HTTP ranges. Completed ranges survive
Pause, errors, and application restarts; incomplete ranges are downloaded again.
Transient connection failures and HTTP 429/5xx responses retry twice with backoff.
The UI reports source, bytes, speed, and estimated time. Installing needs roughly
4.5 GB free because verification assembles the saved ranges into the final file.
The installed model occupies about 2.22 GB. Corrupt files are never published.

## Hosting decision

Keep Hugging Face as the current source. Do not commit weights to the application
Git repository. A separate Git repository would not by itself improve delivery.

For an independently operated mirror, prefer object storage with a CDN, using
immutable paths such as `<revision>/<filename>`, HTTPS, HTTP Range support, and
unchanged file bytes. Check latency and sustained throughput from actual user
regions before choosing a provider. Add a mirror only after it has been deployed
and tested; no mirror or automatic source switching is configured today.

GitHub Releases are suitable for app installers, but each release asset must be
under 2 GiB. This model's weights alone are 2,183,295,977 bytes, exceeding that
limit. Hosting there requires repackaging or sharding and does not guarantee a
faster route for users. See [GitHub's release limits](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases).

Redistributing a mirror also requires carrying the applicable model terms and
notices; see [Gemma terms](https://ai.google.dev/gemma/terms).

## Verification

`ResumableModelDownloadTests` covers response ranges, saved-segment recovery,
and hash failures. Set `TEST_RUNNER_TYPETIDE_TEST_RANGE_DOWNLOAD=1` when running
`xcodebuild test` to additionally download and verify the real 33 MB tokenizer.
The separate full-model/inference test is opt-in because it downloads 2.22 GB.
