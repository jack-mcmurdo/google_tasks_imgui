# Google Tasks C++ App

A fast, standalone C++ desktop application using Dear ImGui to manage Google Tasks —
with optional Google Keep support for Google Workspace accounts. Features seamless
system-browser Google login, subtasks, drag-and-drop, and desktop notifications.

## Installation

Download the latest package for your distribution from the
[Releases page](https://github.com/jack-mcmurdo/google_tasks_imgui/releases):

**Ubuntu / Debian:**
```bash
sudo apt install ./google-tasks-imgui-*-Linux.deb
```

**CentOS / RHEL / Fedora:**
```bash
sudo dnf install ./google-tasks-imgui-*-Linux.rpm
```

Then launch `google-tasks-imgui`.

> A standalone `.tar.gz` is also published. If you use it instead of a package,
> extract it over `/usr` (`sudo tar -xzf google-tasks-imgui-*-Linux.tar.gz -C /usr
> --strip-components=1`) so the app can find its fonts and bundled sign-in
> configuration.

## Signing in

No configuration is needed. On first launch, click **Sign in** — your browser opens
Google's standard login page, and the app is ready as soon as you approve access.
Released packages have everything required for login built in.

> Until the app passes Google's OAuth verification, the consent screen shows an
> "unverified app" warning and sign-ins are capped at 100 users.

Signing in covers Google Tasks. Google Keep is separate, optional, and
Workspace-only — see below.

## Google Keep (Workspace accounts only)

Google Keep has no public API for personal `@gmail.com` accounts, so the Keep section
of the app **only works with a Google Workspace account**, and only after a one-time
setup by your Workspace administrator. Until that's done, the "Keep" sidebar section
simply shows as unavailable — Tasks is unaffected.

### Instructions for your Workspace admin

The following steps are for the **Workspace administrator**, not the app user. They
are done once per Workspace, in the organization's own Google Cloud Console and Admin
Console:

1. In Google Cloud Console, select (or create) the project linked to your Workspace,
   and enable the **Google Keep API**.
2. Create a **Service Account** (IAM & Admin → Service Accounts). Note its **Client ID**
   (not the email) and download a JSON key — treat this file as a credential, not a
   config file.
3. In **Admin Console → Security → Access and data control → API controls →
   Domain-wide delegation**, add the service account's Client ID with scope
   `https://www.googleapis.com/auth/keep`. Do not grant broader scopes than needed.
4. Distribute the JSON key file to each user's machine out-of-band (not via this app's
   installer/package).

**Security notes for the admin:** this key can act as *any* user in the domain for the
Keep scope — restrict its file permissions, never commit it to version control, never
include it in a distributed package/installer, and rotate/revoke it (delete the
domain-wide delegation entry) immediately if it's ever exposed.

### Instructions for you, the app user

Once your admin gives you the JSON key file, make it visible to the app in one of two
ways:

- Set the environment variable `GOOGLE_APPLICATION_CREDENTIALS=/path/to/key.json`
  (most reliable for an installed package), or
- Save it as `keep_service_account.json` in the directory you launch the app from.

That's all — the app impersonates whichever account is currently signed in for Tasks;
there is no separate Keep login step.

### Keep API limitations

Keep's API has real limitations worth knowing before you rely on it:

- **No in-place editing.** The Keep API has never supported updating a note's content.
  "Save" on an existing note deletes it and creates a new one (new ID, reset
  create/update time) — the app surfaces this in the UI rather than hiding it.
- **Deletes are permanent.** Unlike Tasks, the Keep API has no trash/undo for
  `notes.delete` — the app asks for confirmation before deleting a note.
- **No archive/pin/color/labels.** These Keep-app concepts don't exist in the API's
  data model, so they're not supported here.

## Building from source / development

Building, packaging, and OAuth client configuration are developer concerns and live in
[DEVELOPMENT.md](DEVELOPMENT.md). App users installing a released package never need
any of it.

## Acknowledgements

Feature inspiration directly modeled after the robust work done in [google-task-desktop](https://github.com/codad5/google-task-desktop) by author `codad5`.
