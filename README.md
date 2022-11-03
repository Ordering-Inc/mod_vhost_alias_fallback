# mod_vhost_alias_fallback
Based on mod_vhost_alias to add fallback folder

## Compile steps

1. Install apache dev tools
`apt install -y apache2-dev`

2. Compile
`apxs -i -a -c mod_vhost_alias_fallback.c`

## Example
```apache
# Subdomains mydomain.tld

# Load module
LoadModule vhost_alias_fallback_module /usr/lib/apache2/modules/mod_vhost_alias_fallback.so

<VirtualHost *:80>
    ServerAlias *.mydomain.tld

    UseCanonicalName Off
    # Use VirtualDocumentRootWithFallback directive
    VirtualDocumentRootWithFallback /usr/local/apache2/websites/subdomains/%1 usr/local/apache2/websites/subdomains/default
</VirtualHost>
```
