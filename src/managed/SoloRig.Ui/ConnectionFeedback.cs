namespace SoloRig.Ui;

public enum ConnectionFeedbackKind
{
    Neutral,
    Allowed,
    Blocked,
    Diagnostic,
}

public readonly record struct ConnectionFeedback(ConnectionFeedbackKind Kind, string Reason)
{
    public bool IsAllowed => Kind == ConnectionFeedbackKind.Allowed;

    public static ConnectionFeedback Neutral(string reason) =>
        new(ConnectionFeedbackKind.Neutral, reason);

    public static ConnectionFeedback Allowed(string reason) =>
        new(ConnectionFeedbackKind.Allowed, reason);

    public static ConnectionFeedback Blocked(string reason) =>
        new(ConnectionFeedbackKind.Blocked, reason);

    public static ConnectionFeedback Diagnostic(string reason) =>
        new(ConnectionFeedbackKind.Diagnostic, reason);
}
