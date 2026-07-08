# Privacy Policy

**Effective date: July 8, 2026**

This privacy policy covers the **Google Notes** desktop application (binary name
`google-tasks-imgui`, "the app"), published at
<https://github.com/jack-mcmurdo/google_tasks_imgui>.

## The short version

The app is a standalone desktop client for Google services. It has **no servers of
its own**, and it **does not collect, transmit, store, sell, or share any of your
data with the developer or with any third party**. All communication happens
directly between the app running on your computer and Google's APIs, over an
encrypted (TLS) connection. The developer never sees your data and has no way to
access it.

## What data the app accesses, and why

When you sign in with your Google account, the app requests the following OAuth
scopes:

| Scope | Why it is needed |
|---|---|
| `https://www.googleapis.com/auth/tasks` | To read, create, edit, complete, and delete your Google Tasks and task lists — the core purpose of the app. |
| `https://www.googleapis.com/auth/keep` | To read, create, and delete Google Keep notes (optional; only functional for Google Workspace accounts whose administrator has enabled it). |
| `https://www.googleapis.com/auth/userinfo.email` and `.../userinfo.profile` | To display which Google account is currently signed in inside the app. |

This data is fetched from Google's APIs, displayed in the app's window, and — when
you make changes — written back to Google's APIs. Nothing else is done with it.

## What is stored on your computer

- **OAuth tokens** (`tokens.json`): after you sign in, the app saves its access and
  refresh tokens in a local file so you don't have to sign in every time. This file
  never leaves your machine. Deleting it signs you out.
- **Window/layout settings** (`imgui.ini`): purely cosmetic UI state.

Your tasks and notes themselves are **not** cached or stored locally; they live in
your Google account.

## What is *not* collected

The app contains no analytics, telemetry, crash reporting, advertising, or tracking
of any kind. It makes no network connections other than to Google's APIs
(`accounts.google.com`, `oauth2.googleapis.com`, `tasks.googleapis.com`,
`keep.googleapis.com`, and related Google endpoints).

## Data sharing

None. No data is shared with the developer or any third party. The only party that
processes your data is Google itself, under
[Google's own Privacy Policy](https://policies.google.com/privacy).

## Revoking access

You can revoke the app's access to your Google account at any time at
<https://myaccount.google.com/permissions>. Additionally, deleting the local
`tokens.json` file removes the stored credentials from your machine.

## Google API Services User Data Policy

The app's use of information received from Google APIs adheres to the
[Google API Services User Data Policy](https://developers.google.com/terms/api-services-user-data-policy),
including the Limited Use requirements. Specifically, data obtained through Google
APIs is used only to provide the app's user-facing task and note management
features, is never transferred to anyone else, and is never used for advertising,
credit-worthiness assessment, or training AI/ML models.

## Changes to this policy

Any changes to this policy will be published in this file in the app's public
source repository, where the full change history is visible.

## Contact

Questions about this policy can be raised by opening an issue at
<https://github.com/jack-mcmurdo/google_tasks_imgui/issues>.
