using System.Windows;

namespace Doio.Kb16.Mapper;

public partial class ApplyConfirmationWindow : Window
{
    private const int MaximumVisibleChanges = 8;

    public ApplyConfirmationWindow(ConfigurationDiffResult difference)
    {
        InitializeComponent();
        CountBadge.Text = difference.ChangedControlCount.ToString();
        DialogTitle.Text = $"确认应用 {difference.ChangedControlCount} 处修改";
        SummaryText.Text = "写入前会重新读取设备，并检查快照是否仍然一致。";
        ChangeList.ItemsSource = difference.Changes.Take(MaximumVisibleChanges).ToArray();
        var remaining = difference.ChangedControlCount - MaximumVisibleChanges;
        if (remaining > 0)
        {
            RemainingText.Text = $"另有 {remaining} 项变更";
            RemainingText.Visibility = Visibility.Visible;
        }
        DiagnosticText.Text = $"BINARY DIFF  {difference.ByteDifference} BYTES";
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        AppIdentity.ApplyWarmTitleBar(this);
    }

    private void Apply_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = true;
    }
}
