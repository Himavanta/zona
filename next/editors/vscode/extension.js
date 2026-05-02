const vscode = require('vscode');
const path = require('path');
const fs = require('fs');

// ─── Resolve :use'd module files ────────────────────────────────

function collectModuleFiles(document) {
    const files = [];
    const dir = path.dirname(document.uri.fsPath);
    for (let i = 0; i < document.lineCount; i++) {
        const m = document.lineAt(i).text.match(/:use\s+\w+\s+'([^']+)'/);
        if (m) files.push(path.resolve(dir, m[1]));
    }
    return files;
}

// ─── Scan a file for definition lines ───────────────────────────

function findDefinitionInFile(fileUri, word) {
    if (!fs.existsSync(fileUri.fsPath)) return null;
    const text = fs.readFileSync(fileUri.fsPath, 'utf-8');
    const lines = text.split('\n');

    // Patterns to match: @ name, :bind name, :struct Name
    // Word boundary ensures exact match (e.g. "fib" doesn't match "fib2")
    const wordPat = '(?<=^|\\s)' + word.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '(?=\\s|:|;)';

    // 1. Word definition: @ name ... ;
    const wordRe = new RegExp('^@\\s+' + wordPat);
    // 2. FFI binding: :bind name 'cName' ...
    const bindRe = new RegExp('^:bind\\s+' + wordPat);
    // 3. Struct: :struct Name ...
    const structRe = new RegExp('^:struct\\s+' + wordPat);

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (wordRe.test(line) || bindRe.test(line) || structRe.test(line)) {
            return new vscode.Location(fileUri, new vscode.Position(i, 0));
        }
    }
    return null;
}

// ─── Activation ─────────────────────────────────────────────────

function activate(context) {
    // ── Document Link Provider (:use path click) ──
    context.subscriptions.push(
        vscode.languages.registerDocumentLinkProvider(
            { language: 'zona' },
            {
                provideDocumentLinks(document) {
                    const links = [];
                    const dir = path.dirname(document.uri.fsPath);
                    for (let i = 0; i < document.lineCount; i++) {
                        const m = document.lineAt(i).text.match(/:use\s+\w+\s+'([^']+)'/);
                        if (m) {
                            const qStart = m.index + m[0].indexOf("'");
                            const qEnd = qStart + m[1].length + 2;
                            const target = vscode.Uri.file(path.resolve(dir, m[1]));
                            links.push(new vscode.DocumentLink(
                                new vscode.Range(i, qStart, i, qEnd), target));
                        }
                    }
                    return links;
                }
            }
        )
    );

    // ── Definition Provider (Ctrl+Click → definition) ──
    context.subscriptions.push(
        vscode.languages.registerDefinitionProvider(
            { language: 'zona' },
            {
                provideDefinition(document, position) {
                    const wordRange = document.getWordRangeAtPosition(position);
                    if (!wordRange) return null;
                    const word = document.getText(wordRange);
                    if (!word) return null;

                    // Primitive like :dup — no user definition to jump to
                    if (word.startsWith(':')) return null;

                    // Handle member access: "math.abs" → module "math", word "abs"
                    let moduleName = null, wordName = word;
                    const dotIdx = word.indexOf('.');
                    if (dotIdx > 0) {
                        moduleName = word.substring(0, dotIdx);
                        wordName = word.substring(dotIdx + 1);
                    }

                    // Collect files to search: current document + :use'd modules
                    const files = [document.uri];
                    if (!moduleName) {
                        // No module prefix: also search all :use'd module files
                        for (const f of collectModuleFiles(document)) {
                            files.push(vscode.Uri.file(f));
                        }
                    }

                    // If module name specified, find that module's file
                    if (moduleName) {
                        const dir = path.dirname(document.uri.fsPath);
                        for (let i = 0; i < document.lineCount; i++) {
                            const m = document.lineAt(i).text.match(
                                new RegExp(':use\\s+' + moduleName + '\\s+\'([^\']+)\''));
                            if (m) {
                                files.length = 0; // discard other files
                                files.push(vscode.Uri.file(path.resolve(dir, m[1])));
                                break;
                            }
                        }
                    }

                    // Search all candidate files
                    for (const uri of files) {
                        const loc = findDefinitionInFile(uri, wordName);
                        if (loc) return loc;
                    }
                    return null;
                }
            }
        )
    );
}

function deactivate() {}

module.exports = { activate, deactivate };
