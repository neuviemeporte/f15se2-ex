'use strict';

const element = id => document.getElementById(id);
const status = message => { element('status').textContent = message; };
let runtime = null;
let started = false;
let saving = false;
const MAX_IMPORT_BYTES = 128 * 1024 * 1024;

function saveStorage() {
    if (saving) return Promise.reject(new Error('A save is already in progress.'));
    saving = true;
    return new Promise((resolve, reject) => {
        runtime.FS.syncfs(false, error => {
            saving = false;
            if (error) reject(error); else resolve();
        });
    });
}

async function importFiles(files) {
    const candidates = [...files];
    const model = candidates.find(file => file.name.toUpperCase() === '15FLT.3D3');
    if (!model) throw new Error('Choose the original game folder containing 15FLT.3D3.');
    const relativePath = file => file.webkitRelativePath || file.name;
    const modelPath = relativePath(model);
    const prefix = modelPath.slice(0, modelPath.length - model.name.length);
    const gameFiles = candidates.filter(file => {
        const path = relativePath(file);
        return path.startsWith(prefix) && !path.slice(prefix.length).includes('/');
    });
    let totalBytes = 0;
    const names = new Set();
    for (const file of gameFiles) {
        const name = file.name.toUpperCase();
        totalBytes += file.size;
        if (names.has(name) || name === '.' || name === '..' || /[\\/\0]/.test(name))
            throw new Error('Duplicate or invalid filename: ' + name);
        names.add(name);
    }
    if (totalBytes > MAX_IMPORT_BYTES) throw new Error('Game folder exceeds the 128 MiB import limit.');
    // Read everything first so a failed browser read does not erase existing saves.
    const contents = [];
    for (const file of gameFiles) contents.push([file.name.toUpperCase(), new Uint8Array(await file.arrayBuffer())]);
    for (const name of runtime.FS.readdir('/game')) {
        if (name !== '.' && name !== '..') runtime.FS.unlink('/game/' + name);
    }
    for (const [name, data] of contents) runtime.FS.writeFile('/game/' + name, data);
    await saveStorage();
    status('Game files imported. Ready to fly.');
    element('start').disabled = false;
}

element('files').onchange = async event => {
    element('start').disabled = true;
    element('files').disabled = true;
    try { await importFiles(event.target.files); }
    catch (error) { status('Import failed: ' + error.message); }
    finally { element('files').disabled = false; }
};

element('start').onclick = () => {
    if (!runtime || started) return;
    started = true;
    element('start').disabled = true;
    element('files').disabled = true;
    element('canvas').focus();
    status('Flight running. Saves are synchronized every five seconds.');
    runtime.callMain(['--game', '/game']);
};

element('save').onclick = async () => {
    try { await saveStorage(); status('Saved to browser storage.'); }
    catch (error) { status('Save failed: ' + error.message); }
};

element('backup').onclick = () => {
    const name = runtime.FS.readdir('/game').find(name => name.toUpperCase() === 'HALLFAME');
    if (!name) { status('No HallFame pilot file exists yet.'); return; }
    const blob = new Blob([runtime.FS.readFile('/game/' + name)]);
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = 'HallFame';
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
};

element('fullscreen').onclick = async () => {
    try { await element('screen').requestFullscreen(); element('canvas').focus(); }
    catch (error) { status('Fullscreen unavailable: ' + error.message); }
};

createF15Game({
    canvas: element('canvas'),
    noInitialRun: true,
    print: message => console.log(message),
    printErr: message => console.error(message),
    onAbort: message => status('Runtime stopped: ' + message),
    onExit: () => {
        saveStorage().then(() => status('Game closed and saved. Reload to play again.'))
            .catch(error => status('Game closed; save failed: ' + error.message));
    }
}).then(module => {
    runtime = module;
    if (module.storageError) throw new Error('Browser storage unavailable: ' + module.storageError);
    element('files').disabled = false;
    element('save').disabled = false;
    element('backup').disabled = false;
    const imported = runtime.FS.analyzePath('/game/15FLT.3D3').exists;
    element('start').disabled = !imported;
    status(imported ? 'Saved game folder restored. Ready to fly.' : 'Choose your original game folder.');
    setInterval(() => {
        if (started && !saving) saveStorage().catch(error => status('Autosave failed: ' + error.message));
    }, 5000);
}).catch(error => status('Unable to start: ' + error.message));
