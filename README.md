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
3. Fetch every GitHub branch into a private source-tracking namespace
   (`refs/nonforceful-github/heads/*`) with one non-forced `git fetch`, and every tag GitHub has
   and the workspace does not yet have (`refs/nonforceful-github/tags/*`) with explicit refspecs.
   A rewritten branch or a moved tag is refused by Git; the local copy keeps the history it
   already had. A refusal is then reported prominently and makes the run exit non-zero — detecting
   rewrites is the point of the tool, so one is never reported as an ordinary success.
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
* **No forced fetch either.** The branch fetch refspec is exactly
  `refs/heads/*:refs/nonforceful-github/heads/*` — no leading `+`, no `--force`, no
  `--force-with-lease`. A rewritten GitHub branch is therefore *refused by Git*, and the
  source-tracking ref keeps the history the workspace already had. Ordinary fast-forward updates
  are unaffected.
* `git remote add` writes a forced refspec of its own
  (`+refs/heads/*:refs/remotes/<remote>/*`). Both remotes have theirs removed before anything is
  fetched or pushed, so no forced refspec exists anywhere in a workspace, and no
  `refs/remotes/<remote>/*` ref is updated behind the program's back.
* Tags are never fetched with a wildcard refspec. Git accepts a tag that moved *forward* as an
  ordinary fast-forward update even without a forced refspec, so a wildcard would silently replace
  an existing tag. Each tag is fetched with its own explicit refspec, and only while the local tag
  does not exist; an existing local tag is never fetched again, whatever GitHub did to it.
* No `git reset --hard`, no `git clean`, no `--mirror`, no `--delete`, no `--prune`, no
  `--prune-tags`, no `gh repo sync --force`. The fetch is additionally started with `--no-prune`
  and `--no-prune-tags` so that a global `fetch.prune`/`fetch.pruneTags` cannot prune anything.
* **Destination branches and tags are never deleted, never force-updated and never reset.** A
  non-fast-forward branch update or a tag that already points at a different object is *rejected by
  Git*, recorded as a conflict, and left exactly as GitLab had it.
* A destination ref that has no corresponding GitHub ref (because the source branch or tag was
  deleted) is reported as a stale destination ref and **left in place**.
* **No ref is ever deleted, in either repository.** A GitHub branch or tag that disappears leaves
  its source-tracking ref and its GitLab counterpart in place: they are reported (as
  `kept (GitHub no longer has it)` and as a stale destination ref) and kept as backup data. There
  is no pruning step and no `update-ref -d` in the synchronization path.
* There is no archival ref namespace and no archival synchronization. The persistent bare
  repository plus the GitLab project *are* the backup.
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

In those cases the local workspace keeps the previous state — the rewrite is refused at fetch time,
not only at push time — and the destination keeps it too. Every case is reported per ref. This is
intentional: the application never resolves a conflict by overwriting or deleting a ref in either
repository. A rewrite is not silent either: the repository is reported as
`UPSTREAM DIVERGED (nothing forced)`, the report opens with a dedicated list of the rewritten refs,
and the run exits non-zero (see §7) — while a ref that GitHub merely deleted stays informational,
because nothing was rewritten. If you really want the mirror to follow a rewritten source, resolve
it yourself: delete the source-tracking ref and the GitLab ref (or reset the GitLab ref), and the
next run picks up GitHub's current history again.

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
disabled (`GIT_TERMINAL_PROMPT=0`) so an unattended run cannot block, and the child locale is pinned
to `LC_ALL=C` so that the reason text of a refused ref update is always the same.

## 7. Usage

```sh
NonForcefulGitHub2GitLabMirror
```

The program takes no arguments. It prints the absolute local data directory at startup and in the
final report, then processes every discovered repository.

Process exit status:

* `0` – every discovered repository synchronized, every ref is in place (up-to-date refs count as
  success), gap-free discovery, and **GitHub rewrote nothing**. A source ref that GitHub simply no
  longer has does count as success: it was not rewritten, and the local and GitLab copies are kept
  deliberately.
* `1` – at least one repository or ref failed, a project-name collision occurred, a visibility
  discrepancy was found, discovery was incomplete, authentication/tool checks failed, **or GitHub
  rewrote a branch / moved a tag**. A refused rewrite is the correct *action* and nothing is forced,
  but it is not a successful *run*: the point of the tool is to notice rewrites, so a `diverged`
  repository keeps the exit status non-zero on every run until the divergence is resolved by hand.
  The last line of the output says `Result: GITHUB HISTORY WAS REWRITTEN (...)`, naming the number
  of refs and repositories.
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

### Fetching: branches with one command, tags one refspec at a time

The persistent bare repository is a second copy of GitHub's history, so it refuses to take over a
rewritten one. Branches are fetched with one non-forced wildcard:

```sh
git fetch --no-tags --no-prune --no-prune-tags github \
  'refs/heads/*:refs/nonforceful-github/heads/*'
```

No `+`, no `--force`. Git therefore updates a branch only when the new value is a fast-forward of
the old one. When GitHub was rewritten, Git refuses the update, prints
`! [rejected] ... (non-fast-forward)`, the source-tracking ref keeps the value it had, and the run
reports the refusal per branch and per repository (`UPSTREAM DIVERGED (nothing forced)`, exit status
1). That is the whole protection: the rewritten history never enters the workspace, so it can never
reach GitLab either.

Tags are treated differently on purpose. A wildcard tag refspec would be applied by Git to tags
that already exist, and Git accepts a tag that moved **forward** to a descendant commit as an
ordinary fast-forward update — no `+` needed. A wildcard would therefore replace an existing tag
silently. Instead, the tag listing is compared with the local tags first:

* a tag GitHub has and the workspace does not → fetched with one explicit refspec
  `refs/tags/<name>:refs/nonforceful-github/tags/<name>`, 200 per command;
* a tag that is identical on both sides → nothing to do;
* a tag that exists locally at a different object → **not fetched at all**, reported
  `update refused (upstream tag moved)`, and the local tag keeps its object;
* a local tag GitHub no longer has → reported `kept (GitHub no longer has it)` and kept.

Every tag is therefore fetched exactly once, ever, and no tag update — fast-forward or not — can
replace an existing local tag.

`--no-tags` stops Git from quietly adding tags outside the tracking namespace. `--no-prune` and
`--no-prune-tags` are passed explicitly so that a global `fetch.prune`/`fetch.pruneTags` cannot
prune a tracking ref; the program never prunes either. A source ref that disappears on GitHub is
reported, never deleted, and its GitLab counterpart is left in place.

### Fetch outcomes are decisions, not guesses

Whether a ref was created, updated, refused or kept is decided by comparing object ids — the
`git ls-remote` listing of GitHub, the tracking namespace read before the fetch, and the namespace
read afterwards — never by parsing console text. Git's status table is only used for the *reason*
text of a refusal, and only after the comparison has already established that the update was
refused. `LC_ALL=C` is set for every child process so that reason classification does not depend on
the machine's language.

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

Fetch results appear in the same report block, in the same shape, above the push lines:

```
  fetch: 3 branch(es), 2 tag(s) (1 created, 1 updated, 1 unchanged, 1 refused, 1 kept without a GitHub ref)
    source branch dev: update refused (non-fast-forward) [GitHub rewrote the branch; the local tracking ref and the GitLab branch were kept]
    source tag v1: update refused (upstream tag moved) [GitHub moved the tag; the local tag and the GitLab tag were kept]
    source tag old: kept (GitHub no longer has it) [GitHub no longer has this tag; the local and GitLab copies were kept]
```

Only refs that were refused, kept, or failed are listed; `created`, `updated` and
`already up to date` are counted in the header. A refusal is not a failure of the *fetch phase* —
the mirror is in exactly the state it is designed to be in, and the other refs are still pushed —
but it is never a silent success either: the repository becomes `diverged` rather than
`synchronized`, the run's summary opens with a `GitHub history was rewritten (nothing was forced)`
block naming each affected repository and ref, and the exit status is 1. A ref that could not be
fetched at all (transport, permissions) *is* a failure: it is listed as `failed`, the remaining refs
are still pushed, and the repository is counted as completed-with-failures. A repository with both a
rewrite and a real failure is counted as completed-with-failures, so the failure is not masked, and
the rewrite block and the `Result: GITHUB HISTORY WAS REWRITTEN` line are still printed.

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
* how many source ref updates were refused because GitHub rewrote a branch or moved a tag
  (`GitHub rewrites detected (nothing forced, old refs kept): N`), and how many source refs GitHub
  no longer has (informational: those refs are kept),
* when any rewrite was refused, a `GitHub history was rewritten (nothing was forced)` block right
  under the counters, naming every repository and ref concerned (`repo: branch dev (update refused
  (non-fast-forward))`), plus a closing line explaining that the previous history is still on both
  sides and how to resolve it,
* the number of name collisions,
* a per-repository block for **every** discovered repository, including skipped and failed ones, with
  its project outcome, local repository state, fetch result, one line per branch and per tag, stale
  destination refs, notes and the error category. A push command that failed *as a whole* is shown
  on its own `branch push failed [...]` / `tag push failed [...]` line, next to — never instead of —
  the per-ref lines it did report. Refused or kept *source* refs are listed as
  `source <kind> <name>: ...` lines inside the same block, and are never counted as failures,
* a phase timing block with the wall-clock cost of repository discovery, local repository setup,
  source fetch, destination ref listing, the bulk branch push, the bulk tag push, report generation,
  and the total before the report was rendered.

A run in which GitHub had rewritten exactly one branch of one otherwise unchanged repository —
nothing forced, every other ref already in place — ends like this (elided lines marked `...`):

```
Repositories discovered: 4
Repositories synchronized: 3
Repositories with failures: 0
Repositories skipped: 0
...
GitHub rewrites detected (nothing forced, old refs kept): 1
Source refs GitHub no longer has (kept as backup): 0
Name collisions: 0

GitHub history was rewritten (nothing was forced)
-------------------------------------------------
example/project: branch main (update refused (non-fast-forward))
The GitLab refs and the local source-tracking refs still hold the history this
mirror had before the rewrite. Resolve each one by hand (README: "what happens when
GitHub is rewritten"); until then every run exits non-zero.

Repository details
------------------
...
example/project [UPSTREAM DIVERGED (nothing forced)]
  ...
  fetch: 1 branch(es), 1 tag(s) (0 created, 0 updated, 1 unchanged, 1 refused)
    source branch main: update refused (non-fast-forward) [GitHub rewrote the branch; the local tracking ref and the GitLab branch were kept]
    branch main: already up to date [up to date]
  error: GitHub rewrote 1 source ref(s); each one was left as it was and nothing was forced. The GitLab ref and the local backup still hold the previous history -- resolve the rewrite by hand (see the README) (GitHub history rewritten)
  ...

Result: GITHUB HISTORY WAS REWRITTEN (1 ref(s) in 1 repository(ies)) -- nothing was forced, see "GitHub history was rewritten" above
```

The exit status is 1. Nothing was forced and nothing was lost — but the run is not reported as a
success, because the rewrite is the one fact the operator must not miss.

## 13. Verification performed during development

No test suite, test framework or test dependency is shipped. Verification was performed manually
during development with throw-away harnesses.

### Non-forced fetch rework: local integration checks (no network, no credentials)

Two throw-away harnesses were used. The first links the real `GitWorkspace` (with the real
`ProcessRunner`) against local bare repositories that stand in for GitHub and GitLab, and passes a
`git` wrapper as the executable so that **every argument vector the application passes to Git is
recorded and audited**. 1227 checks, built with `-fsanitize=address,undefined`, all green:

* the branch fetch refspec is exactly `refs/heads/*:refs/nonforceful-github/heads/*`; no recorded
  `fetch` or `push` argument starts with `+`, and none is `--force`, `-f`, `--force-with-lease`,
  `--prune`, `--prune-tags`, `--mirror`, `--delete`, `--tags` or `--all`; every fetch carries
  `--no-prune` and `--no-tags`;
* a tag wildcard refspec (`refs/tags/*`) is never passed to Git, and each of the five tags used
  during the run appears in exactly one tag refspec — a tag is fetched once, ever;
* `prepare()` removes the forced refspecs `git remote add` writes, so the workspace has neither a
  `remote.github.fetch` nor `refs/remotes/*`, and the first fetch creates only the tracking
  namespace;
* first fetch: 1 branch and 2 tags created, one annotated tag stored as its tag object (id compared
  with GitHub's), 2 `git fetch` processes (one branch fetch, one tag batch);
* ordinary fast-forward: the branch is reported `updated`, the tracking ref advances to GitHub's
  object id, and only one fetch process runs when there is nothing new to fetch;
* upstream rewrite (`git commit --amend` + force-push): reported `update refused
  (non-fast-forward)`, the tracking ref keeps its previous object id, and the branch is still in the
  push list so a missing destination copy would be filled from the backup;
* a tag moved **forward** (the case a wildcard refspec would accept silently): reported `update
  refused (upstream tag moved)`, local tag unchanged, and no fetch of that tag is ever attempted;
* a tag moved **backwards**: same result;
* a brand-new annotated tag: created, tag object preserved;
* a branch and a tag deleted on GitHub: reported `kept (GitHub no longer has it)`, the complete
  tracking-ref inventory is byte-identical before and after, both are excluded from the push lists,
  and nothing is deleted;
* a partial fetch (one rewritten branch plus one new branch in the same command): the new branch is
  created, the rewrite is refused, the fetch is still `succeeded` and both refs stay in the push
  list;
* a repeated fetch is idempotent (nothing created or updated) and keeps reporting the pending
  refusals;
* the push side is unchanged: a divergent destination branch is `rejected (non-fast-forward)` and
  its object id is untouched, and a conflicting destination tag is `rejected (conflicting tag)` and
  untouched;
* a source remote that cannot be listed makes the fetch fail cleanly (no refs, nothing pushed);
* the fetch status parser, on captured real output and on synthetic lines: the `!`, `*`, `-` and
  unmarked fast-forward rows, the `(reason)` split, and `fatal:` lines.

The second harness runs the shipped binary end to end against stub `gh`/`glab` executables and local
bare repositories (no network, no credentials), four runs of one repository:

1. **fresh**: project created, 1 branch and 2 tags pushed,
   `GitHub rewrites detected (nothing forced, old refs kept): 0`, `Result: SUCCESS`, exit status 0;
   the workspace has no `remote.github.fetch` and no `refs/remotes` namespace, and
   `refs/remotes/gitlab/*` is *not* created by the push (see below);
2. **GitHub rewrite**: reported as `source branch main: update refused (non-fast-forward)`, the
   repository as `UPSTREAM DIVERGED (nothing forced)`, `Repositories synchronized: 0`, a
   `GitHub history was rewritten (nothing was forced)` block naming `fixture/project: branch main
   (update refused (non-fast-forward))`, the closing line
   `Result: GITHUB HISTORY WAS REWRITTEN (...)`, and **exit status 1**; the GitLab branch still
   holds the pre-rewrite commit and the workspace backup matches it;
3. **destination divergence**: `branch main: rejected (non-fast-forward)`,
   `Branch conflicts/failures: 1`, `Repositories with failures: 1`, exit status 1, and the
   destination keeps its history; the rewrite headline is still printed, followed by
   `The run reported other problems as well`, so a real failure is never hidden behind it;
4. **GitHub tag deletion**: `tag light: kept (GitHub no longer has it)`, and the tag survives in
   both the workspace and GitLab. This run still exits 1, because the branch rewritten in run 2 is
   still divergent — the intended behaviour: an unresolved rewrite is reported on every run, while
   a *deletion* alone would not change the exit status.

Two facts were established empirically while building this, because they decide the design:

* a non-forced **tag** wildcard does not protect an existing tag: when the new tag target is a
  descendant of the old one, `git fetch` prints an ordinary fast-forward line and moves the ref.
  Only not fetching the tag can protect it;
* `git push` to a *local* remote applies that remote's configured (forced) fetch refspec, so
  `refs/remotes/gitlab/<branch>` was silently moved by a push. Removing the forced refpecs of both
  remotes is what stops it.

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
* a branch deleted on GitHub was dropped from the source-tracking namespace in that revision (the
  later non-forced fetch rework keeps it instead), was no longer enumerated, and its destination
  branch survived and was reported stale;
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
* a branch deleted on the source was no longer enumerated and was dropped from the private tracking
  namespace in that revision (the later non-forced fetch rework keeps it), and its destination
  branch survived;
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
* final report content and exit statuses (0 for a clean run, 1 when anything failed or when GitHub
  had rewritten a ref).

### Not verified

* No authenticated run against github.com or gitlab.com was performed for the non-forced fetch
  rework: every check above uses local bare repositories, because the behaviour under test
  (refusal of a rewritten branch or a moved tag, retention of deleted refs) is entirely local Git
  semantics and provoking it on a real forge would mean force-pushing over a real repository. The
  push side of the same binary was verified against gitlab.com in the earlier rework (below) and is
  unchanged by this one.
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
* A rewritten branch or a moved tag stays refused on **every** run until it is resolved by hand: the
  workspace never accepts the new history, so it never reaches GitLab either. Every such run exits
  1 and names the affected refs; the exit status therefore stays non-zero until the divergence is
  dealt with, which is deliberate. Resolving it means deleting the source-tracking ref and the GitLab ref (or resetting the
  GitLab ref) yourself.
* Concurrent runs of the program against the same data directory are not supported: there is no
  lock, and two runs would fetch into the same tracking namespace. (Two runs racing on the *first*
  fetch of a brand-new tag could, in principle, let one of them apply a tag update the other did not
  expect; the sequential design has no such window.)
* `refs/remotes/<remote>/*` refs written by earlier versions of this program are left in place —
  nothing is ever deleted — but they are no longer created or updated.

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
src/gitworkspace.h/.cpp            bare workspaces, non-forced fetch, two bulk non-forced pushes
src/fetchstatus.h/.cpp             parser for the reason text of git fetch status lines
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
