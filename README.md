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
   it when it does not exist, with the visibility rule described below — and switch CI/CD off on a
   project that was just created.
2. Locate or create a persistent **bare** Git repository as the synchronization workspace.
3. Fetch every GitHub branch and every GitHub tag into a private source-tracking namespace
   (`refs/nonforceful-github/heads/*`, `refs/nonforceful-github/tags/*`) with a single `git fetch`.
4. Push all branches with one ordinary, non-forced `git push` and all tags with another — one Git
   process per repository and namespace, however many refs there are.
5. Record the outcome per repository and per ref, continue with the next repository, and print a
   comprehensive report at the end.

Archived repositories and forks are included. Only repositories the API actually refuses to return
can be missing.

## 2. Safety guarantees

These are invariants of the implementation, not options:

* No `git push --force`, `-f` or `--force-with-lease`. The only push invocations in the code base
  are `git push --porcelain <remote> refs/nonforceful-github/heads/*:refs/heads/*` and
  `git push --porcelain <remote> refs/nonforceful-github/tags/*:refs/tags/*`, and a guard rejects
  any destructive or forced option — and any refspec that starts with `+` — before a process is
  started.
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
* The only GitLab setting ever written is `builds_access_level=disabled`, and only on a project
  this run created itself, immediately after creation. Existing projects are never reconfigured.

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

### Fetching: one command per repository

Each repository is fetched exactly once per run, with both explicit refspecs in that one command:

```sh
git fetch --quiet --no-tags github \
  '+refs/heads/*:refs/nonforceful-github/heads/*' \
  '+refs/tags/*:refs/nonforceful-github/tags/*'
```

Every branch and every tag — lightweight and annotated — therefore lands in the private
source-tracking namespaces in a single network round trip. The leading `+` applies **only** to
those local namespaces: it lets them follow a rewritten GitHub history, and it can never force
anything on GitLab. `--no-tags` stops Git from quietly adding tags outside the explicit refspec;
the tag refspec itself is what retrieves them. `--prune` and `--prune-tags` are never used: a
source ref that disappears on GitHub is *not* proof of anything until the pre-fetch `git ls-remote`
of the source has succeeded, and even then the destination keeps the ref (it is reported as stale).

### Pushing: one command per namespace

Pushing costs one Git process per repository for branches and one for tags, no matter how many
refs there are:

```sh
git push --porcelain gitlab 'refs/nonforceful-github/heads/*:refs/heads/*'
git push --porcelain gitlab 'refs/nonforceful-github/tags/*:refs/tags/*'
```

* Both refspecs are passed as **single literal arguments**, and no shell is ever involved, so the
  `*` is expanded by Git itself and can never be expanded by a command interpreter.
* The left side is the local source-tracking ref fetched from GitHub, the right side the
  destination ref of the same name; the wildcard maps each matching name to itself.
* Branches and tags are separate commands so their results are classified independently. Neither
  `--tags` nor a combined refspec is used as a substitute for the explicit tag-tracking namespace.
* `--atomic` is not used: a single conflicting ref must not stop unrelated refs from being updated,
  and Git already updates each ref independently.
* The original tag ref is pushed, so annotated tag objects and their messages are preserved.

### Reading the result

Individual outcomes are taken from Git's documented machine-readable `--porcelain` output,
`<flag> \t <from>:<to> \t <summary> (<reason>)`, never from human-readable or localised text.
Every documented flag maps onto the existing per-ref model:

| porcelain flag | Git meaning | reported as |
| --- | --- | --- |
| `' '` | fast-forward | `pushed` |
| `'*'` | new branch or tag | `pushed` |
| `'='` | already up to date | `already up to date` |
| `'!'` + `(non-fast-forward)` or `(fetch first)` | branch update refused | `rejected (non-fast-forward)` |
| `'!'` + `(already exists)` on a tag | tag already points elsewhere | `rejected (conflicting tag)` |
| `'!'` + anything else (`remote rejected`, hook, permissions, transport) | other rejection | `failed` |
| `'-'` | deletion | `failed` — a deletion is never requested |
| `'+'` | forced update | `pushed`, **plus** an operation-level error (this program never forces) |
| any other flag | undocumented | `unknown (git reported no result)` |

The process exit status, standard output and standard error are captured separately. A non-zero
exit status does **not** mean every ref failed: a push that carries one rejected and one updated
ref exits non-zero and still reports the updated ref as `pushed`. Everything Git did report is
kept, and a failure of the command as a whole is added as a *separate* operation-level error
rather than by mislabelling the refs:

* a push process that could not be started, or that failed before printing any ref line, produces
  an operation-level error and leaves every requested ref `unknown`;
* output lines that do not match the documented format, an undocumented status flag, or output
  that ends without the final `Done` marker are reported as uncertainty — the affected refs stay
  `unknown` and the raw line is quoted in the error instead of being interpreted;
* a rejection that Git reported per ref needs no operation-level error at all: the per-ref line
  already states exactly what happened.

Removed source refs are never deleted on GitLab; they are listed as stale destination refs.

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
* A project **created by this run** has CI/CD disabled immediately after creation, before the first
  ref is pushed. This is the equivalent of

  ```
  glab api 'projects/<namespace>%2F<project>' --method PUT --raw-field builds_access_level=disabled
  ```

  and it keeps the pushed branches from triggering pipelines. The response is checked, and a
  project that GitLab does not report back with `builds_access_level=disabled` is reported as
  `CI/CD: not disabled` with the reason. A failed CI/CD change never aborts the mirror — the refs are
  still synchronized and the report states exactly what happened.
* A project that **already existed** is never reconfigured: its CI/CD setting, visibility and every
  other setting are left exactly as they are. The program only ever writes settings of a project it
  created itself in the same run.

## 12. The final report

After every repository has been processed the program prints:

* the absolute local data directory,
* repositories discovered / synchronized / with failures / skipped,
* GitLab projects created, and how many of them had CI/CD disabled,
* branches pushed (and how many were already up to date) and branch conflicts/failures,
* tags pushed (and how many were already up to date) and tag conflicts/failures,
* how many `git push` processes were needed, with the branch/tag split — two per repository is the
  norm, one per namespace, regardless of the number of refs,
* the number of name collisions,
* a per-repository block for **every** discovered repository, including skipped and failed ones, with
  its project outcome, local repository state, fetch result, one line per branch and per tag, stale
  destination refs, notes and the error category. A push command that failed *as a whole* is shown
  on its own `branch push failed [...]` / `tag push failed [...]` line, next to — never instead of —
  the per-ref lines it did report,
* a phase timing block with the wall-clock cost of repository discovery, local repository setup,
  source fetch, destination ref listing, the bulk branch push, the bulk tag push, report generation,
  and the total before the report was rendered.

## 13. Verification performed during development

No test suite, test framework or test dependency is shipped. Verification was performed manually
during development with throw-away harnesses.

### Bulk push rework: local integration checks (no network, no credentials)

A second throw-away harness drove the real `GitWorkspace` against local bare repositories that stood
in for GitHub and GitLab; 92 checks, all asserted against actual destination refs and object ids:

* the porcelain parser, on both captured real output and synthetic strings: `' '`, `'*'`, `'='`,
  `'!'` and `'-'` lines, bracket stripping, `(reason)` splitting, the `To <url>` header, the `Done`
  marker, empty output, a push that matched no ref, unparseable lines, an undocumented status flag
  and a truncated stream;
* one fetch populates both source-tracking namespaces, including lightweight and annotated tags;
* one branch push command creates every branch, and one tag push command creates every tag
  (`commands == 1`, refs pushed by one process);
* the annotated tag reaches the destination as a tag object with its message intact, while a
  lightweight tag stays a lightweight tag;
* an unchanged mirror reports every ref as `already up to date` and exits its command successfully;
* a fast-forward is accepted and followed;
* a rewritten source branch is `rejected (non-fast-forward)` while the *other* branches in the same
  command are still updated, and the rejected destination branch is byte-identical afterwards;
* a moved tag is `rejected (conflicting tag)` while a new tag in the same command is created, and the
  conflicting destination tag is not overwritten;
* destination-only branches and tags are reported as stale, left in place, and a push over unchanged
  refs leaves the destination inventory bit-identical;
* a branch deleted on GitHub is dropped from the source-tracking namespace, is no longer enumerated,
  and its destination branch survives and is reported stale;
* a failed fetch returns no refs (so nothing is pushed for it) and leaves every destination ref
  untouched;
* an empty ref list launches no Git process at all;
* the rendered report maps a rejected branch, a stale ref, an operation-level push error, the push
  command count and the phase timings onto the existing report model.

### Bulk push rework: authenticated end-to-end checks (gitlab.com)

Run against one throw-away private GitHub repository (`SnoutFirst/nfg2glm-bulk-verify`: branches
`main`, `dev` and `feature/slashy`, a lightweight tag `light` and an annotated tag `v1`) plus the
GitLab project the program created for it. Discovery was restricted to that single repository with a
development-time `gh` wrapper. Four complete runs:

1. **first run** — project created, `CI/CD: disabled`, 3 branches and 2 tags pushed by **2**
   `git push` processes, exit status 0;
2. **second run** — every branch and tag `already up to date`, still exactly 2 push commands,
   exit status 0;
3. **rejection run** — `dev` rewritten on GitHub and a new commit on `main`, while the GitLab
   `main` branch was protected with "allowed to push: no one": the single branch command reported
   `branch dev: rejected (non-fast-forward)` and
   `branch main: failed [remote rejected (pre-receive hook declined)]`, while `feature/slashy` and
   both tags stayed `already up to date`, with exit status 1. GitLab's branch and tag listing was
   byte-identical before and after the run: nothing was moved, forced or deleted;
4. **recovery run** — after restoring push access to `main`, the same bulk command fast-forwarded
   it (`branch main: pushed [3de2155..c9b6111]`) while `dev` stayed rejected — a rejected ref does
   not stop the others, and it heals itself once the cause is gone.

A final confirmation run with the exact binary built from this tree repeated the first scenario on a
second throw-away repository (`SnoutFirst/nfg2glm-bulk-verify2`, branches `main` and `topic/x`, an
annotated and a lightweight tag): project created with CI/CD disabled, 4 refs pushed by 2 commands,
exit status 0, and the destination ref inventory byte-identical to GitHub's — including the
annotated tag object.

Both GitHub repositories were deleted afterwards; the GitLab projects were deleted through the API
and are in gitlab.com's pending-deletion state (renamed to `…-deletion_scheduled-<id>`, renamed out
of the namespace again when GitLab finishes the job).

### Bulk push rework: measured effect

81 refs (61 branches and 20 tags) mirrored into a local bare destination, three runs of each
strategy, same refs and identical destination inventories (branch/tag listing plus object ids
compared by checksum):

| strategy | run 1 | run 2 | run 3 | Git processes |
| --- | --- | --- | --- | --- |
| one `git push` per ref (previous behaviour) | 7.43 s | 7.43 s | 7.43 s | 81 |
| one `git push` per namespace (this rework) | 0.37 s | 0.36 s | 0.35 s | 2 |

That is roughly a 20× reduction locally. Each per-ref push also spawns a `git receive-pack` on the
destination side, and over HTTPS every push is its own authenticated round trip, so the saving
against a real forge is larger than the local figure: in the authenticated run above, 3 branches
were pushed in one command in 2.3–3.1 s and 2 tags in 1.7–3.3 s.

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
  `public`, followed by `builds_access_level=disabled` on the new project;
* the exact `glab` invocation used to disable CI/CD (`PUT /projects/<url-encoded path>` with
  `--raw-field builds_access_level=disabled`) issued against an existing project on gitlab.com
  (`SnoutFirst/glib`, already `disabled`, so no state changed) and confirmed to be accepted and
  reflected in the response;
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
* A deliberate full run against the complete repository list of the account was **not** performed:
  that would create and populate projects for every repository of the account, which is outside what
  unrequested test activity should do. Enumeration itself was verified against the complete list.
  (While exercising the empty-run report path, the account's *existing* mirror was traversed once by
  accident. Every ref came back `already up to date`, `last_activity_at` did not move on any of the
  affected GitLab projects, and the destination inventories were verified identical to GitHub's — so
  nothing was written — but it is not counted as verification.)
* Ancillary GitLab features (protected branches, merge request settings, LFS, submodules) are not
  handled; only refs are synchronized. The single exception is `builds_access_level=disabled` on
  projects created by the run.
* Disabling CI/CD was verified against a freshly created project in the bulk-push run (the project
  really came back with `builds_access_level=disabled` and CI/CD never ran), but the failure path
  (GitLab refusing the setting) was still not provoked on a real instance.
* The remote-rejection path was verified against a *protected branch* on gitlab.com. Other
  server-side rejections (quota, namespace permissions, push rules on a paid tier) were not
  provoked; they are classified as `failed`, exactly like the protected-branch case.

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
src/gitworkspace.h/.cpp            bare workspaces, one bulk fetch and two bulk non-forced pushes
src/pushporcelain.h/.cpp           parser for git push --porcelain ref status lines
src/storage.h/.cpp                 PlatformFolders based data directory and workspace naming
src/syncresult.h/.cpp              structured result types
src/reporting.h/.cpp               final report
src/textutil.h/.cpp                small string helpers
LICENSE.md                         MIT license text
```

## 16. License

Released under the MIT license. Copyright (c) 2026 d3fault <d3fault@d3fault.net> — see
[LICENSE.md](LICENSE.md).
