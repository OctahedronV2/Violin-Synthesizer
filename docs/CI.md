# Continuous integration

CI is one workflow, `.github/workflows/build.yml`. It is set up to give a quick answer on Windows, where the plugin is tried, and to skip work that a change cannot affect.

## What runs when

| Job | Runs on | Runs when |
|---|---|---|
| Plan | Linux | Always (a few seconds). Decides which jobs below run. |
| clang-format | Linux | A `.h`, `.cpp` or `.clang-format` file changed. |
| Research model tests | Linux | Something under `research/` changed. |
| RealtimeSanitizer | Linux | C++ code, tests, CMake, resources or `third_party/` changed. |
| Benchmark (instruction counts) | Linux | Code the benchmark measures, `tests/bench/` or `docs/benchmarks/` changed. |
| Windows | Windows | Same as RealtimeSanitizer. Builds, runs the unit tests, pluginval and Steinberg's validator, and uploads the plugin. |
| Linux, macOS | Linux, macOS | Same files as Windows, but only for version tags (`v*`), runs started by hand, and pull requests labelled `all-platforms`. |
| Publish release | Linux | Version tags (`v*`), or a run started by hand on `main` with **release** ticked, after every other job passed. Creates the GitHub Release with the plugin zips for all three platforms, using `docs/releases/<tag>.md` as the notes. |

A change to the workflow file runs everything. Markdown-only changes run nothing.

Pull requests and pushes to `main` run CI. A branch without a pull request can be built from **Actions > Build > Run workflow**, choosing the branch; a run started this way checks everything, on all three platforms by default.

## Checking Linux and macOS on a pull request

Add the `all-platforms` label. That starts a run with just Linux and macOS (Windows already ran for the latest push), and every later push to the pull request then builds all three. Remove the label to go back to Windows only.

## Why it is faster

- **Windows only by default.** Linux and macOS ran alongside Windows, so this saves runner time more than waiting time.
- **Compile cache.** All builds go through [sccache](https://github.com/mozilla/sccache), stored in the GitHub Actions cache. JUCE and unchanged source files are not recompiled. Each build job prints its cache hit rate at the end.
- **Ninja on Windows.** Needed for sccache, and it keeps all four cores busy. Each build job also prints its slowest build steps, to guide further tuning.
- **No link-time optimisation on pull requests.** With it, every link (plugin, standalone, tests, benchmark) redoes code generation for all of JUCE: about 4 of the 6 build minutes on Windows, and the compile cache can't help. Without it the synth needs about 9% more instructions (measured with the Callgrind benchmark), so pull request builds work the same but use a little more CPU. Builds of `main`, tags and runs started by hand keep it, and so does the Callgrind job, so the CPU check still compares like with like. Take downloads for CPU measurements from a `main` build. Locally, `-DVIOLINSYNTH_ENABLE_LTO=OFF` does the same.
- **Path filters.** Jobs whose files did not change are skipped.
- **The Callgrind benchmark has its own job,** which builds only `ViolinSynthBench`, so it no longer needs the full Linux build.
- **Steinberg's validator is built once** per SDK version and platform, and kept in the Actions cache.

## Reusing this for Octastra

The same layout carries over to the orchestra plugin. Change the product and artefact names in the `env` block, and the file lists in the Plan job's filters (for example, a shared `common/` DSP folder would go in every C++ list). The platform choice, the `all-platforms` label, the compile cache and the validator cache need no change. If both plugins end up in one repository, give each its own filter entries and jobs so a change to one plugin does not rebuild the other.

## Making a release

1. Set the version in `project(Octavio VERSION …)` in `CMakeLists.txt` and write the notes in `docs/releases/v<version>.md`, in a pull request.
2. After it is merged, open **Actions > Build > Run workflow**, choose `main`, tick **release** and start it. (Pushing a `v<version>` tag to that commit does the same.)
3. The run checks everything on all three platforms, then the Publish release job tags the commit and creates the release with the zips attached. If the release was already made by hand on GitHub, the job only attaches the zips.
