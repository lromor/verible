[first-extension]: https://code.visualstudio.com/api/get-started/your-first-extension
[vscode-lsp]: https://code.visualstudio.com/api/language-extensions/language-server-extension-guide
[packaging]: https://code.visualstudio.com/api/working-with-extensions/publishing-extension#packaging-extensions
[clangd]: https://github.com/clangd/vscode-clangd/blob/master/src/clangd-context.ts

# Visual Studio Code Extension

## Installation
Be sure your system as a recent version of both [nodejs](https://nodejs.org/) and [npm](https://www.npmjs.com/).
From the current directory, run `npm install`.

After [installing the verible tools](../../../README.md#installation), run `npm run vsix` to generate the extension file `verible.vsix`.
Finally, run `code --install-extension verible.vsix` to install the extension.

# References

- [Your first extension][first-extension]
- [Language server extension guide][vscode-lsp]
- [Packaging the extension][packaging]
- [Clangd example][clangd]
