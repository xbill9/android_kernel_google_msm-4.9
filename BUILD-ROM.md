# Build, test and install LineageOS 24.0 on the Pixel 3a

One command, described in
[backport-tools/lineage-24.0/README.md](backport-tools/lineage-24.0/README.md):

    sargo update    # pull LineageOS and kernel fixes, build, test on the phone, publish on PASS
    sargo install   # install the newest release on the phone; marks it Latest if it passes
    sargo test      # test a kernel change in progress on the phone, without building the ROM

Run `sargo update` in tmux (`tmux new -s rom`). When `sargo install` prints `>>> ON THE PHONE`,
tap **Yes** at "Signature verification failed", then **Reboot system now**.

- A laptop that does not build, or a first install: [INSTALL.md](INSTALL.md).
- A new build machine: [backport-tools/lineage-24.0/BUILD.md](backport-tools/lineage-24.0/BUILD.md).
- Copying the tree between the two build machines: [SYNC-AND-PULL.md](SYNC-AND-PULL.md).
- Committing changes to the forks: [GIT-WORKFLOW.md](GIT-WORKFLOW.md).
