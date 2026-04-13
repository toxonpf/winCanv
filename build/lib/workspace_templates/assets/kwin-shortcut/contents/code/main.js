/*
    SPDX-License-Identifier: MIT
*/

const SERVICE = "io.github.toxonpf.workspacetemplates";
const PATH = "/io/github/toxonpf/workspacetemplates";
const INTERFACE = "io.github.toxonpf.workspacetemplates";

registerShortcut(
    "WorkspaceTemplatesTogglePanel",
    "Toggle Workspace Templates panel",
    "Meta+W",
    function () {
        callDBus(SERVICE, PATH, INTERFACE, "TogglePanel");
    }
);
