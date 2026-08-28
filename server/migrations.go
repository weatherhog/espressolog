// Package espressolog (module root) embeds the SQL migrations so the
// binary is self-contained — go:embed paths are file-relative, and the
// migrations directory lives here.
package espressolog

import "embed"

//go:embed migrations/*.sql
var Migrations embed.FS
