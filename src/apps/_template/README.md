Template for new apps: `tools/new-app.sh <id> "<Name>" <Icon>` copies this folder to
`src/apps/<id>/` and fills in the `__PLACEHOLDERS__`. This folder is excluded from every build
(`build_src_filter` in `platformio.ini`).
