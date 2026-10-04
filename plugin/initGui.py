"""Optional native adapter: absence leaves the host's Reader path available."""

try:
    import NatronGui
    import postproject_reader
    from qtpy.QtWidgets import QApplication

    NatronGui.natron.addMenuCommand(
        "PostProject/Associate Reader…", "postproject_reader.menu_associate"
    )
    NatronGui.natron.addMenuCommand(
        "PostProject/Resolve sequence…", "postproject_reader.menu_resolve"
    )
    NatronGui.natron.addMenuCommand(
        "PostProject/Refresh knowledge", "postproject_reader.menu_refresh"
    )
    NatronGui.natron.addMenuCommand(
        "PostProject/Verify sequence", "postproject_reader.menu_verify"
    )
    NatronGui.natron.addMenuCommand(
        "PostProject/Cancel resolution", "postproject_reader.cancel_all"
    )
    QApplication.instance().aboutToQuit.connect(postproject_reader.cancel_all)
except ImportError:
    pass
