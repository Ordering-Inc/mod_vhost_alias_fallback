# mod_vhost_alias_fallback
Based on mod_vhost_alias to add fallback folder

Compile steps

1. Install apache dev tools
`apt install -y apache2-dev`

2. Compile
`apxs -i -a -c mod_vhost_alias_fallback.c`
