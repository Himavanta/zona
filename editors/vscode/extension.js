const vscode = require('vscode');
const path = require('path');

function activate(context) {
    const provider = vscode.languages.registerDocumentLinkProvider(
        { language: 'zona' },
        {
            provideDocumentLinks(document) {
                const links = [];
                const dir = path.dirname(document.uri.fsPath);

                for (let i = 0; i < document.lineCount; i++) {
                    const line = document.lineAt(i).text;
                    const re = /:use\s+'([^']+)'/g;
                    let m;
                    while ((m = re.exec(line)) !== null) {
                        const target = vscode.Uri.file(path.resolve(dir, m[1]));
                        const q0 = m.index + ':use '.length;          // opening quote
                        const q1 = q0 + m[1].length + 2;             // closing quote
                        const range = new vscode.Range(i, q0, i, q1);
                        links.push(new vscode.DocumentLink(range, target));
                    }
                }
                return links;
            }
        }
    );

    context.subscriptions.push(provider);
}

function deactivate() {}

module.exports = { activate, deactivate };
