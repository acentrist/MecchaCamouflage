using System.Diagnostics;
using MecchaCamouflage.Core;

namespace MecchaCamouflage.WebHost;

internal enum WebViewRecoveryAction
{
    Close,
    Retry
}

internal static class WebViewFailureDialog
{
    public static WebViewRecoveryAction Show(
        IWin32Window owner,
        LocalizationCatalog localization,
        string locale,
        string message,
        string diagnostics,
        string manualInstallUrl,
        bool retryAvailable)
    {
        using var dialog = new Form
        {
            Text = localization.Text(locale, "app.title"),
            StartPosition = FormStartPosition.CenterParent,
            FormBorderStyle = FormBorderStyle.FixedDialog,
            MinimizeBox = false,
            MaximizeBox = false,
            ShowInTaskbar = false,
            ClientSize = new Size(620, 280),
            AutoScaleMode = AutoScaleMode.Dpi
        };

        var heading = new Label
        {
            AutoSize = false,
            Left = 18,
            Top = 18,
            Width = 584,
            Height = 52,
            Text = message,
            Font = new Font(dialog.Font, FontStyle.Bold)
        };
        var description = new Label
        {
            AutoSize = false,
            Left = 18,
            Top = 76,
            Width = 584,
            Height = 34,
            Text = localization.Text(locale, "dialog.webview.details")
        };
        var manualInstallLink = new LinkLabel
        {
            AutoSize = true,
            Left = 18,
            Top = 114,
            Text = localization.Text(locale, "dialog.webview.download")
        };
        manualInstallLink.LinkClicked += (_, _) =>
        {
            try
            {
                Process.Start(new ProcessStartInfo(manualInstallUrl) { UseShellExecute = true });
            }
            catch
            {
                // The diagnostic text remains available if the default browser cannot be opened.
            }
        };
        var details = new TextBox
        {
            Left = 18,
            Top = 142,
            Width = 584,
            Height = 88,
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            Text = diagnostics
        };
        var close = new Button
        {
            Text = localization.Text(locale, "button.close"),
            DialogResult = DialogResult.Cancel,
            Left = 527,
            Top = 244,
            Width = 75
        };
        dialog.CancelButton = close;
        dialog.Controls.AddRange([heading, description, manualInstallLink, details, close]);

        if (retryAvailable)
        {
            var retry = new Button
            {
                Text = localization.Text(locale, "button.retry.once"),
                DialogResult = DialogResult.Retry,
                Left = 435,
                Top = 244,
                Width = 84
            };
            dialog.AcceptButton = retry;
            dialog.Controls.Add(retry);
        }

        return dialog.ShowDialog(owner) == DialogResult.Retry
            ? WebViewRecoveryAction.Retry
            : WebViewRecoveryAction.Close;
    }
}
