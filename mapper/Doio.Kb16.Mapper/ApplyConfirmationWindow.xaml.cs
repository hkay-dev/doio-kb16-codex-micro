using System.Windows;

namespace Doio.Kb16.Mapper;

public partial class ApplyConfirmationWindow : Window
{
    private const int MaximumVisibleChanges = 8;

    public ApplyConfirmationWindow(ConfigurationDiffResult difference)
    {
        InitializeComponent();
        CountBadge.Text = difference.ChangedControlCount.ToString();
        DialogTitle.Text = $"Apply {difference.ChangedControlCount} changes?";
        SummaryText.Text = "The Mapper reads the device again and checks that the snapshot still matches.";
        ChangeList.ItemsSource = difference.Changes.Take(MaximumVisibleChanges).ToArray();
        var remaining = difference.ChangedControlCount - MaximumVisibleChanges;
        if (remaining > 0)
        {
            RemainingText.Text = $"{remaining} more changes";
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
