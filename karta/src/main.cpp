#include "karta/cli/cli.h"
#include "karta/core/platform.h"

int main(int argc, char** argv) {
  karta::enable_utf8_console();
  const karta::CliOptions options = karta::parse_args(argc, argv);
  return karta::run_cli(options);
}
