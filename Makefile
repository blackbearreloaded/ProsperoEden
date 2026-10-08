# SPDX-License-Identifier: GPL-3.0-or-later
# ProsperoEden build. `make` builds the release files in dist/; `make help` lists every target.
# Missing dependencies are fetched at their pinned revisions (tools/deps.json); nothing that
# already exists is changed. The build runs on Linux (Ubuntu 26.04; WSL works).

SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := release
MAKEFLAGS += --no-print-directory

# Parallel compile jobs.
JOBS ?= $(shell nproc)
export EDEN_BUILD_JOBS := $(JOBS)
# make dev: the title ID (16 hex digits) the development build boots.
DEV_TITLE ?= $(shell cat .local/dev-profile-title 2>/dev/null)
# make install: the console's address, and the port of its FTP server.
PS5_HOST ?=
PS5_PORT ?= 2121

PYTHON := python3 -B
RELEASE_APP := build/release/PPSA99008

.PHONY: help release package image dev prepare deps deps-status toolchain test install clean distclean

help: ## List the targets and variables
	@echo 'ProsperoEden build - make [target] [VARIABLE=value]'
	@echo
	@awk 'BEGIN { FS = ":.*## " } /^[a-z][a-z-]*:.*## / { printf "  %-12s %s\n", $$1, $$2 }' $(MAKEFILE_LIST)
	@echo
	@echo 'Variables:'
	@echo '  JOBS=$(JOBS)  parallel compile jobs'
	@echo '  DEV_TITLE=$(DEV_TITLE)  title ID the development build boots (make dev)'
	@echo '  PS5_HOST=$(PS5_HOST) PS5_PORT=$(PS5_PORT)  console for make install'

release: package ## Release files in dist/: ZIP, .ffpfsc image, SHA256SUMS, release notes (default)
	$(PYTHON) tools/ci/make-dist.py $(RELEASE_APP)

package: prepare ## Build the app as released: build/release/PPSA99008
	bash tools/build-package.sh release

image: package ## Only the ShadowMountPlus package image in dist/
	$(PYTHON) tools/ci/make-dist.py --image-only $(RELEASE_APP)

dev: prepare ## Development build that boots DEV_TITLE: build/dev/PPSA99008
	@[[ -n "$(DEV_TITLE)" ]] || { echo 'Set DEV_TITLE=<16-digit title ID> (or write it to .local/dev-profile-title)'; exit 2; }
	bash tools/build-package.sh dev $(DEV_TITLE)

prepare: deps ## Build cache, Eden source, FFmpeg, packaging tool, driver stub, RADV (what is missing)
	bash tools/prepare-build.sh

deps: ## Fetch missing dependencies at their pinned revisions
	$(PYTHON) tools/deps.py fetch

deps-status: ## Show every dependency, where it lives and whether it matches its pin
	@$(PYTHON) tools/deps.py status

toolchain: ## Check the host tools the build needs
	@bash tools/check-toolchain.sh

test: deps ## Host (Linux) build of the emulator and its test suites (ROMM_CHECK=1: also against RomM servers in Docker)
	bash tools/check-elevation-client.sh
	bash tools/build-headless-host.sh

install: ## Copy build/release/PPSA99008 to a console over FTP (PS5_HOST=<address>)
	@[[ -n "$(PS5_HOST)" ]] || { echo 'Set PS5_HOST=<console address>'; exit 2; }
	$(PYTHON) tools/install-ftp.py $(PS5_HOST) $(RELEASE_APP) --port $(PS5_PORT)

clean: ## Remove build outputs (build/, dist/); dependencies and the build cache stay
	rm -rf build dist

distclean: clean ## Also remove the fetched dependencies in .deps and this checkout's build cache
	$(PYTHON) tools/deps.py clean
	@if [[ -f .local/headless-cache ]]; then \
		cache=$$(cat .local/headless-cache); \
		if [[ $$cache == */ps5-eden-headless.* && -f $$cache/owner && $$(cat $$cache/owner) == $(CURDIR) ]]; then \
			echo "rm -rf $$cache"; rm -rf "$$cache"; \
		fi; \
		rm -f .local/headless-cache; \
	fi
