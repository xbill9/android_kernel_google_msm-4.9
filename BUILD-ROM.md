# Build, test and install LineageOS 24.0 on the Pixel 3a

Four commands, described in
[backport-tools/lineage-24.0/README.md](backport-tools/lineage-24.0/README.md):

    sargo-build        # sync LineageOS, rebase the forks, build, publish a prerelease
    sargo-install      # install the newest release on the phone; marks it Latest if it passes
    sargo-test --push  # RAM-boot test of the kernel at HEAD, nothing flashed; pushes on PASS
    backport-tools/check-upstream.sh    # upstream kernel fixes this tree lacks

Run `sargo-build` and `sargo-test` in tmux (`tmux new -s rom`). When `sargo-install` prints
`>>> ON THE PHONE`, tap **Yes** at "Signature verification failed", then **Reboot system now**.

- A laptop that does not build, or a first install: [INSTALL.md](INSTALL.md).
- A new build machine: [backport-tools/lineage-24.0/BUILD.md](backport-tools/lineage-24.0/BUILD.md).
- Copying the tree between the two build machines: [SYNC-AND-PULL.md](SYNC-AND-PULL.md).
- Committing changes to the forks: [GIT-WORKFLOW.md](GIT-WORKFLOW.md).
