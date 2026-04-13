from __future__ import annotations

import argparse


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="workspace-templates")
    parser.add_argument("--standalone", action="store_true", help="Run as a normal window instead of a tray app")
    subparsers = parser.add_subparsers(dest="command")

    save_parser = subparsers.add_parser("save", help="Save the current workspace")
    save_parser.add_argument("--name", required=True, help="Template name")

    load_parser = subparsers.add_parser("load", help="Load a saved workspace")
    load_parser.add_argument("name", help="Template name")
    load_parser.add_argument("--close-existing", action="store_true", help="Close existing windows first")

    delete_parser = subparsers.add_parser("delete", help="Delete a saved template")
    delete_parser.add_argument("name", help="Template name")

    subparsers.add_parser("list", help="List saved templates")
    subparsers.add_parser("toggle-panel", help="Toggle the left workspace drawer")
    return parser


def run_cli(args: argparse.Namespace) -> int:
    from .manager import WorkspaceTemplateManager

    manager = WorkspaceTemplateManager()
    try:
        if args.command == "save":
            result = manager.save_template(args.name)
            print(f"saved {result.name} ({result.window_count} windows) -> {result.path}")
            return 0
        if args.command == "load":
            result = manager.load_template(args.name, close_existing=args.close_existing)
            print(
                f"loaded {result.template_name}: applied={result.applied_count}/{result.requested_windows} "
                f"unmatched={len(result.unmatched_target_ids)} launched={sum(1 for item in result.launched if item.ok)}"
            )
            for item in result.launched:
                if not item.ok:
                    print(f"launch error: {' '.join(item.command) or '<none>'}: {item.message}")
            return 0 if not result.unmatched_target_ids else 1
        if args.command == "delete":
            manager.delete_template(args.name)
            print(f"deleted {args.name}")
            return 0
        if args.command == "list":
            for template in manager.list_templates():
                print(f"{template.name}\t{len(template.windows)}\t{template.created_at}")
            return 0
    finally:
        manager.shutdown()
    return 0


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.command == "toggle-panel":
        from .ui import run_panel_host, send_panel_command

        if send_panel_command("toggle"):
            return 0
        return run_panel_host(show_panel_on_start=True)
    if args.command:
        return run_cli(args)

    from .ui import run_panel_host, run_standalone, send_panel_command

    if args.standalone:
        return run_standalone()
    if send_panel_command("show"):
        return 0
    return run_panel_host(show_panel_on_start=False)


if __name__ == "__main__":
    raise SystemExit(main())
