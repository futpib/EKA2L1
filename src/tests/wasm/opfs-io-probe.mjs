// Instrument a scratch copy of Emscripten's generated JavaScript. The WasmFS
// OPFS imports execute in pthread workers, so page-only hooks miss their calls.
import fs from 'node:fs';

const [input, output] = process.argv.slice(2);
if (!input || !output) throw Error('Usage: node opfs-io-probe.mjs INPUT_JS OUTPUT_JS');
const generated = fs.readFileSync(input, 'utf8');
const marker = 'var wasmImports;';
if (generated.split(marker).length !== 2 || !generated.includes('function __wasmfs_opfs_get_entries'))
  throw Error('Unknown Emscripten OPFS JavaScript layout');

const probe = String.raw`
// Diagnostic-only probe inserted before Wasm imports capture these functions.
{
  const events = [];
  const dirPaths = new Map([[1, '/']]);
  const filePaths = new Map();
  const accessPaths = new Map();
  const blobPaths = new Map();
  globalThis.ekaOpfsTraceEvents = events;
  const pathOf = (map, id) => map.get(id) || 'id:' + id;
  const join = (parent, name) => parent.replace(/\/$/, '') + '/' + name;
  const record = (op, started, path, bytes = 0, offset = -1, result = 0) => {
    events.push({time_us:Math.round((performance.timeOrigin + started) * 1000), duration_us:Math.round((performance.now() - started) * 1000),
      op, path, bytes, offset, result});
  };
  const file = wasmfsOPFSGetOrCreateFile;
  wasmfsOPFSGetOrCreateFile = async function(parent, name, create) {
    const id = await file(parent, name, create);
    if (id > 0) filePaths.set(id, join(pathOf(dirPaths, parent), name));
    return id;
  };
  const dir = wasmfsOPFSGetOrCreateDir;
  wasmfsOPFSGetOrCreateDir = async function(parent, name, create) {
    const id = await dir(parent, name, create);
    if (id > 0) dirPaths.set(id, join(pathOf(dirPaths, parent), name));
    return id;
  };
  const describe = (op, a) => {
    switch (op) {
    case 'get_child': case 'insert_directory': case 'insert_file': case 'remove_child':
      return [join(pathOf(dirPaths, a[1]), UTF8ToString(a[2]))];
    case 'get_entries': return [pathOf(dirPaths, a[1])];
    case 'move_file': return [pathOf(filePaths, a[1])];
    case 'open_access': case 'open_blob': case 'get_size_file': case 'set_size_file':
      return [pathOf(filePaths, a[1])];
    case 'close_access': case 'flush_access': case 'get_size_access': case 'set_size_access':
      return [pathOf(accessPaths, a[1])];
    case 'get_size_blob': return [pathOf(blobPaths, a[0])];
    case 'read_blob': return [pathOf(blobPaths, a[1]), a[3], Number(a[4])];
    case 'read_access': case 'write_access':
      return [pathOf(accessPaths, a[0]), a[2], Number(a[3])];
    default: return ['/'];
    }
  };
  const wrap = (op, original) => function(...a) {
    const [path, bytes, offset] = describe(op, a);
    const started = performance.now();
    const finish = result => {
      if (op === 'open_access') {
        const id = (growMemViews(), HEAP32)[a[2] >> 2];
        if (id > 0) accessPaths.set(id, path);
      } else if (op === 'open_blob') {
        const id = (growMemViews(), HEAP32)[a[2] >> 2];
        if (id > 0) blobPaths.set(id, path);
      }
      record(op, started, path, bytes, offset, typeof result === 'number' ? result : 0);
      return result;
    };
    try {
      const result = original(...a);
      if (result?.then) return result.then(finish, error => { record(op, started, path, bytes, offset, -1); throw error; });
      return finish(result);
    } catch (error) { record(op, started, path, bytes, offset, -1); throw error; }
  };
  __wasmfs_opfs_close_access = wrap('close_access', __wasmfs_opfs_close_access);
  __wasmfs_opfs_flush_access = wrap('flush_access', __wasmfs_opfs_flush_access);
  __wasmfs_opfs_get_child = wrap('get_child', __wasmfs_opfs_get_child);
  __wasmfs_opfs_get_entries = wrap('get_entries', __wasmfs_opfs_get_entries);
  __wasmfs_opfs_get_size_access = wrap('get_size_access', __wasmfs_opfs_get_size_access);
  __wasmfs_opfs_get_size_blob = wrap('get_size_blob', __wasmfs_opfs_get_size_blob);
  __wasmfs_opfs_get_size_file = wrap('get_size_file', __wasmfs_opfs_get_size_file);
  __wasmfs_opfs_init_root_directory = wrap('init_root', __wasmfs_opfs_init_root_directory);
  __wasmfs_opfs_insert_directory = wrap('insert_directory', __wasmfs_opfs_insert_directory);
  __wasmfs_opfs_insert_file = wrap('insert_file', __wasmfs_opfs_insert_file);
  __wasmfs_opfs_move_file = wrap('move_file', __wasmfs_opfs_move_file);
  __wasmfs_opfs_open_access = wrap('open_access', __wasmfs_opfs_open_access);
  __wasmfs_opfs_open_blob = wrap('open_blob', __wasmfs_opfs_open_blob);
  __wasmfs_opfs_read_access = wrap('read_access', __wasmfs_opfs_read_access);
  __wasmfs_opfs_read_blob = wrap('read_blob', __wasmfs_opfs_read_blob);
  __wasmfs_opfs_remove_child = wrap('remove_child', __wasmfs_opfs_remove_child);
  __wasmfs_opfs_set_size_access = wrap('set_size_access', __wasmfs_opfs_set_size_access);
  __wasmfs_opfs_set_size_file = wrap('set_size_file', __wasmfs_opfs_set_size_file);
  __wasmfs_opfs_write_access = wrap('write_access', __wasmfs_opfs_write_access);
}
`;

fs.writeFileSync(output, generated.replace(marker, probe + '\n' + marker));
