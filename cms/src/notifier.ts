import { Alert } from './alerts';

// Fire-and-forget POST of an alert to an outbound webhook (Slack/Discord/generic).
// Failures are logged, never thrown — a flaky webhook must not disrupt the CMS.
export async function notifyAlert(url: string, alert: Alert): Promise<void> {
  try {
    await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ type: 'alert', alert }),
      signal: AbortSignal.timeout(5000),
    });
  } catch (err) {
    console.error('[alert-webhook] post failed:', err instanceof Error ? err.message : err);
  }
}
