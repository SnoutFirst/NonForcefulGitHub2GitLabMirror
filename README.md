# NonForcefulGitHub2GitLabMirror

A cross-platform C++17 command line application that mirrors **every GitHub repository the
authenticated account can reach** into that account's **personal GitLab namespace**, without ever
forcing, overwriting or deleting anything on the GitLab side.

GitHub is the sole source of truth. GitLab is a downstream mirror. The application never pulls
commits or refs from GitLab back into GitHub.

---

## 1. What it does

For every repository returned by the GitHub REST endpoint `/user/repos` with
`affiliation=owner,collaborator,organization_member` (all pages, not just the first one):

1. Resolve the destination GitLab project in the authenticated user's personal namespace, creating
   it when it does not exist, with the visibility rule described below.
2. Locate or create a persistent **bare** Git repository as the synchronization workspace.
3. Fetch every GitHub branch and every GitHub tag into a private source-tracking namespace
   (`refs/nonforceful-github/heads/*`, `refs/nonforceful-github/tags/*`).
4. Push each branch and each tag to GitLab with an ordinary, non-forced `git push`.
5. Record the outcome per repository and per ref, continue with the next repository, and print a
   comprehensive report at the end.

Archived repositories and forks are included. Only repositories the API actually refuses to return
can be missing.

## 2. Safety guarantees

These are invariants of the implementation, not options:

* No `git push --force`, `-f` or `--force-with-lease`. The only push invocation in the code base is
  `git push --porcelain <remote> <src>:<dst>`, and a guard rejects any destructive option before a
  process is started.
* No `git reset --hard`, no `git clean`, no `--mirror`, no `--delete`, no `--prune`, no
  `gh repo sync --force`.
* **Destination branches and tags are never deleted, never force-updated and never reset.** A
  non-fast-forward branch update or a tag that already points at a different object is *rejected by
  Git*, recorded as a conflict, and left exactly as GitLab had it.
* A destination ref that has no corresponding GitHub ref (because the source branch or tag was
  deleted) is reported as a stale destination ref and **left in place**.
* The only refs ever removed are the application's own source-tracking refs below
  `refs/nonforceful-github/`, and they are removed only because GitHub no longer has them. Removing
  a ref never removes an object, a local branch or a tag.
* No shell is involved: `gh`, `glab` and `git` are launched as subprocesses with each argument passed
  separately, so repository names, URLs and ref names can never be re-interpreted by a shell.
* A failing repository, branch or tag never aborts the run.

### What this means for mirror fidelity

GitLab is **not** guaranteed to be byte-for-byte or ref-for-ref identical to GitHub when

* a source branch was rewritten (non-fast-forward), or
* a source tag was moved to a different object, or
* a source branch or tag was deleted.

In those cases the destination keeps the previous state and the conflict is reported. This is
intentional: the application never resolves a conflict by overwriting or deleting destination refs.
Delete the destination ref yourself if you really want the mirror to follow a rewritten source.

## 3. Supported operating systems

* Linux (XDG conventions, `XDG_DATA_HOME` respected)
* macOS (per-user Application Support directory)
* Windows (per-user application-data directory)

Only portable facilities are used: `std::filesystem`, PlatformFolders, Boost.Process and the
standard library. No operating-system specific API is called from application code and no
administrator privileges are required.

## 4. Build requirements

| Requirement | Version / notes |
| --- | --- |
| C++ compiler | C++17 (GCC 9+, Clang 10+, MSVC 2019+) |
| CMake | 3.20 or newer |
| Boost | 1.83 or newer, with the header-only **Boost.Process v2** and the compiled **Boost.Filesystem** and **Boost.JSON** libraries |
| PlatformFolders | fetched automatically by CMake (`FetchContent`) |
| Threads | the platform threading library (Boost.Asio) |

Boost.Process v2 is included in the Boost packages of recent distributions
(`libboost1.83-dev` or newer on Debian/Ubuntu, `boost` on Homebrew, `vcpkg install boost-process`
on Windows). It is used in its header-only configuration; the application links
`Boost::filesystem` (path handling) and `Boost::json` (API response parsing) and links
`Threads::Threads` for Asio.

Install the dependencies on Debian/Ubuntu:

```sh
sudo apt install build-essential cmake libboost1.83-dev libboost-filesystem1.83-dev libboost-json1.83-dev
```

## 5. Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

PlatformFolders is acquired with `FetchContent` and pinned to the immutable commit of release
**4.3.0** (`1df240bdcbfe8b431f3fa224da086fa805b8162c`); no moving branch is tracked. The upstream
test suite and install rules of PlatformFolders are disabled.

Optional install:

```sh
cmake --install build --prefix /usr/local
```

## 6. Required tools and authentication

The application refuses to start a partially authenticated run. Before any repository is touched it
verifies that:

* `git`, `gh` and `glab` are installed and executable (resolved through `PATH`),
* `gh` is authenticated (`gh api /user`) and the account can enumerate repositories,
* `glab` is authenticated (`glab api user`).

```sh
gh auth login          # or: export GH_TOKEN=<token>
glab auth login        # or: export GITLAB_TOKEN=<token>
```

Git credentials are supplied by the CLIs themselves: for every `git fetch`, `git ls-remote` and
`git push` the application passes
`-c credential.helper= -c credential.helper=!"<gh|glab>" auth git-credential`, so the GitHub CLI is
used for GitHub URLs and the GitLab CLI for GitLab URLs. Nothing is written to your Git
configuration and no token ever appears on a command line or in a file. Terminal prompts are
disabled (`GIT_TERMINAL_PROMPT=0`) so an unattended run cannot block.

## 7. Usage

```sh
NonForcefulGitHub2GitLabMirror
```

The program takes no arguments. It prints the absolute local data directory at startup and in the
final report, then processes every discovered repository.

Process exit status:

* `0` – every discovered repository synchronized and every ref is in place (up-to-date refs count as
  success), and discovery was complete.
* `1` – at least one repository or ref failed, a project-name collision occurred, a visibility
  discrepancy was found, discovery was incomplete, or authentication/tool checks failed.
* `2` – command line arguments were supplied.

## 8. Local storage

One bare repository per GitHub repository is kept beneath the per-user application-data directory:

| Platform | Location |
| --- | --- |
| Linux | `$XDG_DATA_HOME/NonForcefulGitHub2GitLabMirror/repositories/` (default `~/.local/share/...`) |
| macOS | `~/Library/Application Support/NonForcefulGitHub2GitLabMirror/repositories/` |
| Windows | `%LOCALAPPDATA%\NonForcefulGitHub2GitLabMirror\repositories\` |

The exact path is printed at startup. The directory is created on demand; filesystem failures are
reported and abort before synchronization starts.

Workspace names are `<owner>--<name>--<12 hex digits>.git`, where the owner and name are
lower-cased and sanitized to `[a-z0-9._-]` and the suffix is an FNV-1a hash of the case-sensitive
`owner/name`. That makes the mapping deterministic and collision-resistant even on case-insensitive
filesystems, and repositories that differ only by case or by non-portable characters can never share
a workspace.

Each workspace records its identity in its own Git config (`nfg2glm.sourcerepository`). An existing
directory that is not a bare repository, or that belongs to a different GitHub repository, is
reported and skipped — never deleted, never recreated, never reset.

## 9. Visibility mapping

| GitHub | GitLab |
| --- | --- |
| `public` | `public` |
| `private` | `private` |
| `internal` or anything else | `private` |

A private GitHub repository can therefore never become a public GitLab project. If an existing
GitLab project has a different visibility than required, the discrepancy is reported and the
visibility is **not** changed; that repository is counted as a completed-with-failures repository and
makes the exit status nonzero.

## 10. Branch and tag handling

* Every branch is pushed as `refs/nonforceful-github/heads/<branch>` →
  `refs/heads/<branch>`, one push per ref.
* Every tag (lightweight and annotated) is pushed as `refs/nonforceful-github/tags/<tag>` →
  `refs/tags/<tag>`. The original tag ref is pushed, so annotated tag objects and their messages are
  preserved.
* Push results are read from `git push --porcelain`: new/updated refs, `up to date`, `rejected
  (non-fast-forward)` and `rejected (already exists)` are distinguished. Anything else is reported as
  a generic failure, so a permission or transport error is never mistaken for a conflict.
* Removed source refs are never deleted on GitLab; they are listed as stale destination refs.

## 11. Project lookup, creation and collisions

* Projects are looked up before they are created, with `GET /projects/:full_path`.
* A failed creation is never taken as proof that the project is absent: the project is looked up
  again before a failure is reported.
* Projects are always created in the authenticated user's personal namespace; the GitLab `path`
  (URL slug) is derived deterministically from the GitHub repository name.
* If two GitHub repositories would map to the same GitLab project, the second one is recorded as a
  collision and skipped — their contents are never merged.
* An existing project is verified to be the expected one (namespace and path) before anything is
  pushed to it.

## 12. The final report

After every repository has been processed the program prints:

* the absolute local data directory,
* repositories discovered / synchronized / with failures / skipped,
* GitLab projects created,
* branches pushed (and how many were already up to date) and branch conflicts/failures,
* tags pushed (and how many were already up to date) and tag conflicts/failures,
* the number of name collisions,
* a per-repository block for **every** discovered repository, including skipped and failed ones, with
  its project outcome, local repository state, fetch result, one line per branch and per tag, stale
  destination refs, notes and the error category.

## 13. Verification performed during development

No test suite, test framework or test dependency is shipped. Verification was performed manually
during development with throw-away harnesses.

### Local Git integration checks (no network, no credentials)

A temporary harness drove the real `GitWorkspace` code against local bare repositories. All of the
following were asserted against actual destination refs and object ids, not console output:

* a branch absent from the destination is pushed, and the destination commit matches the source;
* an unchanged branch and an unchanged tag are reported as up to date;
* a fast-forward update is accepted and followed;
* a rewritten source branch is rejected as non-fast-forward and the destination commit is unchanged;
* an annotated tag is pushed **as the tag object** (verified with `git cat-file -t`) with its message
  intact, and a lightweight tag is pushed;
* a tag moved to a different object on the source is rejected as a tag conflict and the destination
  tag is unchanged;
* stale destination branches and tags are reported and left in place;
* a rejection does not prevent later refs (and later repositories) from being pushed;
* a branch deleted on the source is no longer enumerated, is dropped from the private tracking
  namespace, and its destination branch survives;
* a source repository with no refs at all is handled;
* a workspace belonging to a different repository is rejected and left untouched;
* workspace naming is deterministic and distinguishes owners/names differing only in case.

### Authenticated end-to-end checks

Performed with a real `gh` and `glab` session against two controlled GitHub repositories
(`nfg2glm-verify-private`, `nfg2glm-verify-public`, two branches, one lightweight and one annotated
tag each) and their GitLab projects. Repository enumeration was restricted to those two repositories
by a development-time `gh` wrapper so that unrelated repositories were never mirrored. Results:

* repository enumeration and JSON parsing, including a multi-page walk (verified separately with a
  reduced page size against the full 18-repository list; the code returned all 18 across four pages);
* GitLab project creation in the personal namespace, with `private` → `private` and `public` →
  `public`;
* reuse of the local bare repositories on later runs (`reused`, refs up to date, exit status 0);
* pushing two branches plus a lightweight and an annotated tag to each project;
* a rewritten GitHub branch rejected as non-fast-forward and a moved GitHub tag rejected as a tag
  conflict, with the GitLab refs verified byte-identical (`git ls-remote`) before and after, while the
  other repository continued and was synchronized;
* stale GitLab branches/tags reported and left untouched;
* a source branch deleted on GitHub: destination branch kept and reported stale;
* a GitHub visibility change (public → private) reported as a discrepancy without changing the
  existing public GitLab project, with exit status 1; after restoring the visibility the run returned
  to success;
* final report content and exit statuses (0 for a clean run, 1 when anything failed).

### Not verified

* Windows and macOS builds were not executed; the code path is portable C++17 with no
  platform-specific API calls, but only Linux was compiled and run.
* A GitLab instance other than gitlab.com was not exercised.
* A full run against the complete repository list of the account was **not** performed: that would
  have created and populated projects for every repository of the account, which is outside what
  unrequested test activity should do. Enumeration itself was verified against the complete list.
* Ancillary GitLab features (protected branches, merge request settings, LFS, submodules) are not
  handled; only refs are synchronized.

## 14. Known limitations

* Renamed GitHub repositories are treated as new repositories; the old GitLab project is left alone.
* GitHub repositories whose names map to the same GitLab path are not merged; the second one is
  reported as a collision.
* Protected destination branches (for example a protected `master`) will reject pushes; this is
  reported as a failure and never worked around.
* Only refs (branches and tags) are synchronized: issues, releases, wiki, settings, LFS objects and
  submodules are out of scope.
* The mirror is deliberately not exact when the source is rewritten or refs are deleted, for the
  safety reasons described above.

## 15. Source layout

```
CMakeLists.txt                     build configuration, FetchContent for PlatformFolders
README.md
src/main.cpp                       entry point
src/nonforcefulgithub2gitlabmirror.h/.cpp   the NonForcefulGitHub2GitLabMirror orchestrator
src/processrunner.h/.cpp           Boost.Process based subprocess abstraction
src/githubdiscovery.h/.cpp         paginated GitHub repository discovery
src/githubrepository.h             repository metadata
src/gitlabproject.h/.cpp           GitLab project lookup and creation
src/gitworkspace.h/.cpp            bare workspaces, fetches and non-forced pushes
src/storage.h/.cpp                 PlatformFolders based data directory and workspace naming
src/syncresult.h/.cpp              structured result types
src/reporting.h/.cpp               final report
src/textutil.h/.cpp                small string helpers
```
