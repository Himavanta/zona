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

                    // Match :use name 'path' — named import only
                    const re = /:use\s+\w+\s+'([^']+)'/g;
                    let m;
                    while ((m = re.exec(line)) !== null) {
                        // Find the opening quote position in the match
                        const quoteStart = m[0].indexOf("'");
                        const q0 = m.index + quoteStart;
                        const q1 = q0 + m[1].length + 2;
                        const target = vscode.Uri.file(path.resolve(dir, m[1]));
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
