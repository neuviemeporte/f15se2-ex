// Populate both mounts before main: game files and SDL preference files.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
    addRunDependency('persistent-files');
    for (const path of ['/game', '/libsdl']) {
        FS.mkdirTree(path);
        FS.mount(IDBFS, {}, path);
    }
    FS.syncfs(true, function (error) {
        Module['storageError'] = error;
        removeRunDependency('persistent-files');
    });
});
