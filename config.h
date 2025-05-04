const char *scrtime_path = "/tmp/screentime";
const char *archive_dir = "/home/" USER "/.local/share/screentime";
const char *appid_path = "/tmp/dwl/appid";
const char *title_path = "/tmp/dwl/title";

const struct mapping mappings[] = {
	/* appid		title		your custom app name */
	{ "none",		"none",		"Desktop" },
	{ "foot",		"foot",		"Terminal" },
	{ "foot",		"Neovim",	"Neovim" },
};
