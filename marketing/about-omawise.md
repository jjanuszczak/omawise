# Omawise

Omawise is a native desktop companion for [Readwise Reader](https://readwise.io/read), built for [Omarchy](https://omarchy.org/) and the Arch Linux desktop. It brings your Reader library into a focused, keyboard friendly app that feels at home on an Omarchy system.

The project aims to make everyday reading and library organization quick and comfortable: browse Inbox, Later, Shortlist, Archive, and Feed; search documents; read supported article content; and work with tags, notes, highlights, and highlight annotations. Omawise uses the official Readwise CLI for account access and syncing, so your Readwise account stays the source of truth and the app does not store your token. Its compact interface, keyboard navigation, and Omarchy themed controls are designed to keep attention on the reading and the ideas you save.

Omawise is an early, actively developing project. Its current reader focuses on Markdown and article text. Readwise's specialized PDF and EPUB viewers, offline sync, and some advanced Reader features are not yet implemented.

## Download and install

Find the source code and published packages on the [Omawise GitHub repository](https://github.com/jjanuszczak/omawise). When a release is available, download the x86_64 Arch Linux package from [GitHub Releases](https://github.com/jjanuszczak/omawise/releases/latest), then install it with:

```sh
sudo pacman -U ./omawise-*.pkg.tar.zst
```

Omawise requires the official Readwise CLI. Install it and sign in once:

```sh
npm install -g @readwise/cli
readwise login
```

The app is intended for Omarchy and compatible x86_64 Arch Linux systems. Developers can build from a checkout with `./bin/build`; on Arch, `./bin/install` builds and installs the current checkout.
