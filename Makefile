DOCKER := docker
IMAGE := sms-plugins-devcontainer:local
CONTAINER := sms-plugins-devcontainer
CODEX_VOLUME := sms-plugins-devcontainer-codex
VNC_VOLUME := sms-plugins-devcontainer-vnc
RESOURCE_LABEL := io.sudometalstudio.sms-plugins-devcontainer=true
NOVNC_HOST_PORT ?= 6080
VNC_HOST_PORT ?= 5901

.PHONY: dev-build dev-run dev-ready dev-status dev-shell dev-codex dev-logs \
	dev-vnc-password dev-stop dev-wipe

dev-build:
	$(DOCKER) build \
		--file .devcontainer/Dockerfile \
		--build-arg DEV_UID="$$(id -u)" \
		--build-arg DEV_GID="$$(id -g "$${USER}")" \
		--label $(RESOURCE_LABEL) \
		--tag $(IMAGE) .

dev-run:
	bash .devcontainer/scripts/prepare-volumes.sh
	@if $(DOCKER) container inspect $(CONTAINER) >/dev/null 2>&1; then \
		label_value="$$( $(DOCKER) container inspect --format \
			'{{index .Config.Labels "io.sudometalstudio.sms-plugins-devcontainer"}}' \
			$(CONTAINER) )"; \
		if [ "$$label_value" != true ]; then \
			echo "Refusing to replace unrelated container: $(CONTAINER)" >&2; \
			exit 1; \
		fi; \
		$(DOCKER) rm --force $(CONTAINER) >/dev/null; \
	fi
	$(DOCKER) run --detach \
		--name $(CONTAINER) \
		--label $(RESOURCE_LABEL) \
		--shm-size=1g \
		--cap-drop=ALL \
		--security-opt=no-new-privileges \
		--publish 127.0.0.1:$(NOVNC_HOST_PORT):6080 \
		--publish 127.0.0.1:$(VNC_HOST_PORT):5901 \
		--mount type=bind,source="$(CURDIR)",target=/workspaces/SMS-Plugins \
		--mount type=volume,source=$(CODEX_VOLUME),target=/home/vscode/.codex \
		--mount type=volume,source=$(VNC_VOLUME),target=/home/vscode/.config/sms-plugins-devcontainer \
		--workdir /workspaces/SMS-Plugins \
		$(IMAGE)
	@echo "noVNC: http://127.0.0.1:$(NOVNC_HOST_PORT)/vnc.html?autoconnect=true&resize=scale"
	@echo "VNC:   127.0.0.1:$(VNC_HOST_PORT)"

dev-ready:
	@for ready_attempt in $$(seq 1 30); do \
		if $(DOCKER) exec $(CONTAINER) desktop-health; then exit 0; fi; \
		sleep 1; \
	done; \
	echo "Container desktop did not become ready. Run: make dev-logs" >&2; \
	exit 1

dev-status:
	$(DOCKER) ps --all --filter label=$(RESOURCE_LABEL)
	$(DOCKER) exec $(CONTAINER) desktop-health

dev-shell:
	$(DOCKER) exec --interactive --tty $(CONTAINER) bash

dev-codex:
	$(DOCKER) exec --interactive --tty $(CONTAINER) codex

dev-logs:
	$(DOCKER) logs $(CONTAINER)

dev-vnc-password:
	$(DOCKER) exec --interactive --tty $(CONTAINER) bash -lc '\
		password_tool="$$(command -v tigervncpasswd || command -v vncpasswd)"; \
		"$$password_tool" "$$HOME/.config/sms-plugins-devcontainer/tigervnc.passwd"'
	@echo "Restart the container to enable VNC password authentication."

dev-stop:
	@if ! $(DOCKER) container inspect $(CONTAINER) >/dev/null 2>&1; then \
		echo "Container is not present: $(CONTAINER)"; \
		exit 0; \
	fi; \
	label_value="$$( $(DOCKER) container inspect --format \
		'{{index .Config.Labels "io.sudometalstudio.sms-plugins-devcontainer"}}' \
		$(CONTAINER) )"; \
	if [ "$$label_value" != true ]; then \
		echo "Refusing to remove unrelated container: $(CONTAINER)" >&2; \
		exit 1; \
	fi; \
	$(DOCKER) rm --force $(CONTAINER)

# Close any VS Code remote window first. This removes only resources bearing
# this repository's label, plus the two exact persistent volume names after
# their labels are verified. Removing CODEX_VOLUME requires signing in again.
dev-wipe:
	@echo "Close VS Code's remote window first; Codex sign-in and the VNC password will be removed."
	@container_ids="$$( $(DOCKER) ps -aq --filter label=$(RESOURCE_LABEL) )"; \
	if [ -n "$$container_ids" ]; then \
		printf '%s\n' "$$container_ids" | xargs -r $(DOCKER) rm --force; \
	fi
	@network_ids="$$( $(DOCKER) network ls -q --filter label=$(RESOURCE_LABEL) )"; \
	if [ -n "$$network_ids" ]; then \
		printf '%s\n' "$$network_ids" | xargs -r $(DOCKER) network rm; \
	fi
	@image_ids="$$( $(DOCKER) image ls -q --filter label=$(RESOURCE_LABEL) )"; \
	if [ -n "$$image_ids" ]; then \
		printf '%s\n' "$$image_ids" | sort -u | xargs -r $(DOCKER) image rm --force; \
	fi
	@for volume in $(CODEX_VOLUME) $(VNC_VOLUME); do \
		if ! $(DOCKER) volume inspect "$$volume" >/dev/null 2>&1; then continue; fi; \
		label_value="$$( $(DOCKER) volume inspect --format \
			'{{index .Labels "io.sudometalstudio.sms-plugins-devcontainer"}}' \
			"$$volume" )"; \
		if [ "$$label_value" != true ]; then \
			echo "Refusing to remove unlabeled volume: $$volume" >&2; \
			exit 1; \
		fi; \
		$(DOCKER) volume rm "$$volume"; \
	done
