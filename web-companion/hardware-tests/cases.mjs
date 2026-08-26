#!/usr/bin/env node
/**
 * Hardware parity test cases for the web-companion rebuild
 * (openspec/changes/web-companion-tooling tasks 6.1 - 6.8).
 *
 * These flows cannot be automated: Web Bluetooth has no automatable device
 * picker, and the flows that matter (fingerprint scans, the setup window,
 * an OTA that reboots the device) are physical. Each case names its
 * preconditions, the exact steps to perform in the deployed companion, and
 * the expected outcomes - the runner records a verdict per case.
 */

export const cases = [
	{
		id: 'HW-6.1',
		task: '6.1',
		title: 'Full first-time setup on a wiped device',
		preconditions: [
			'Device factory-reset (setup state: not started, unfiltered advertising)',
			'Companion open at the deployed URL (or localhost dev server)'
		],
		steps: [
			'Connect to the device via the Connect screen.',
			'Confirm the setup wizard opens (not the login form).',
			'Step 1: press "Enrol Admin Finger" and complete 3 scans.',
			'Step 2: register an account (name + password) and confirm with the Admin finger.',
			'Step 3: put the Nuki lock in pairing mode and press "Start Nuki Pairing".',
			'Step 4: press "Finish Setup".'
		],
		expected: [
			'Wizard (not login) is shown for the unclaimed device.',
			'Enrolment progress advances step-by-step (3 scans), then advances to registration.',
			'Registration confirmation advances to Nuki pairing.',
			'Nuki pairing succeeds and advances to finish.',
			'Finish reports the device is claimed and switched to filtered advertising.',
			'Reconnecting afterwards shows the login form, not the wizard.'
		]
	},
	{
		id: 'HW-6.2',
		task: '6.2',
		title: 'Wizard resume: reconnect within the setup window',
		preconditions: [
			'Device mid-setup (at least through Admin enrolment), setup window still open'
		],
		steps: [
			'Disconnect (or walk out of range) mid-wizard, after at least one completed step.',
			'Reconnect within the setup window.',
			'Observe which wizard step is offered.'
		],
		expected: [
			'The wizard resumes at the step the reported setup state implies.',
			'Completed steps are NOT repeated.',
			'The step indicator line names the reported state.'
		]
	},
	{
		id: 'HW-6.3',
		task: '6.3',
		title: 'Setup lapse: window elapses mid-wizard',
		preconditions: [
			'Device mid-setup, setup window about to elapse (~15 min per arm)'
		],
		steps: [
			'Disconnect mid-wizard and let the setup window elapse.',
			'Try to reconnect (the device should no longer be visible / connectable).',
			'Press the device button to re-arm, then reconnect.'
		],
		expected: [
			'After the lapse the app reports the connection loss and states that the setup window elapsed, progress was discarded, and the button must be pressed to re-arm.',
			'After re-arm the wizard starts over at Admin enrolment (no residual progress).'
		]
	},
	{
		id: 'HW-6.4',
		task: '6.4',
		title: 'Login on a claimed device, rejected login reveals nothing',
		preconditions: ['Device claimed and setup-complete, at least one registered account'],
		steps: [
			'Log in with valid credentials.',
			'Log out / reconnect, then submit a WRONG password for an existing user.',
			'Submit credentials for a user name that does not exist.'
		],
		expected: [
			'Valid credentials reach the dashboard.',
			'Both rejected attempts show the SAME generic message ("Incorrect username or password.") with no indication of whether the username exists.',
			'No stack trace or raw error is surfaced for either rejection.'
		]
	},
	{
		id: 'HW-6.5a',
		task: '6.5',
		title: 'User management: enrol verb with a real admin scan',
		preconditions: ['Logged in on a claimed device as admin'],
		steps: [
			'Dashboard > Enroll Fingerprint: pick a free User ID and permission, press Enroll.',
			'Scan the authorizing Admin finger, then complete the 3 enrolment scans.'
		],
		expected: [
			'Status walks through: waiting for Admin scan -> authorized -> 3 enrolment steps.',
			'Completion reports success with the new User ID.',
			'Refresh Users lists the new user with the chosen permission.'
		]
	},
	{
		id: 'HW-6.5b',
		task: '6.5',
		title: 'User management: delete verb with a real admin scan',
		preconditions: ['Logged in; at least one deletable (non-last-admin) user exists'],
		steps: [
			'User Management > Delete on a user.',
			'Scan the Admin finger on the device.'
		],
		expected: [
			'Deletion completes and reports "User <id> deleted."',
			'The refreshed user list no longer contains the user.'
		]
	},
	{
		id: 'HW-6.5c',
		task: '6.5',
		title: 'User management: permission change with a real admin scan',
		preconditions: ['Logged in; a non-admin user exists'],
		steps: [
			'User Management > Permission on a Standard user, set to Admin (3).',
			'Scan the Admin finger.',
			'Repeat in reverse (Admin -> Standard) on another user.'
		],
		expected: [
			'Permission change completes ("Permission updated for user <id>.") and the list reflects it.',
			'Demotion of the LAST admin is refused with the specific last-admin message.'
		]
	},
	{
		id: 'HW-6.5d',
		task: '6.5',
		title: 'User management: rename with a real admin scan',
		preconditions: ['Logged in; a renamable user exists'],
		steps: [
			'User Management > Rename on a user; enter a new unique name.',
			'Scan the Admin finger.',
			'Try renaming to a name that is already taken.'
		],
		expected: [
			'Rename completes and the list shows the new name.',
			'The name-taken attempt shows the SPECIFIC "name already used" message, not a generic failure.'
		]
	},
	{
		id: 'HW-6.5e',
		task: '6.5',
		title: 'User management: denied scan rendered differently from timeout',
		preconditions: ['Logged in; a non-admin finger available to scan'],
		steps: [
			'Start any admin-gated verb (e.g. delete) and scan a NON-admin finger.',
			'Start another admin-gated verb and scan nothing; let the ~10 s device window lapse.',
			'Compare the two status messages.'
		],
		expected: [
			'The denied scan shows the specific "Denied: the fingerprint scanned was not an admin finger." message.',
			'The lapsed window shows the specific "Timed out: no admin fingerprint was scanned on the device." message.',
			'The two outcomes are visually/textually distinct.'
		]
	},
	{
		id: 'HW-6.6',
		task: '6.6',
		title: 'Self-affecting change warnings do not block the change',
		preconditions: ['Logged in as an admin; a second admin exists (or accept losing authority)'],
		steps: [
			'Attempt to delete or demote the user whose name matches the logged-in account.',
			'Confirm the warning dialog.',
			'Complete the authorizing scan.'
		],
		expected: [
			'A warning naming your OWN user appears before the request is sent.',
			'Confirming proceeds with the change (the warning does not block it).',
			'Cancelling the warning sends nothing.'
		]
	},
	{
		id: 'HW-6.7',
		task: '6.7',
		title: 'Health view updates from notifications',
		preconditions: ['Logged in on a claimed device'],
		steps: [
			'Watch the health table and status cards without pressing Refresh.',
			'Toggle the lock (or trigger a state change) at the device.',
			'Compare a field the device does not measure (if any) and a lock state right after a command you sent.'
		],
		expected: [
			'Health values update from notifications without a manual refresh.',
			'Fields with no reading display "Unknown" (never a number or a carried-over value).',
			'A lock state derived from a sent command shows "(awaiting confirmation)" until confirmed.',
			'Readings older than ~60 s are surfaced with their age.'
		]
	},
	{
		id: 'HW-6.8',
		task: '6.8',
		title: 'OTA transfer to completion, with mid-transfer disconnect resume',
		preconditions: [
			'Device battery reported above 20% (or accept the risk for the test)',
			'A valid signed .bin firmware image',
			'Note: a mid-transfer disconnect will require reconnect + re-login to resume'
		],
		steps: [
			'Start an OTA transfer with a valid image and let it run to completion.',
			'After the reboot, reconnect and verify the new firmware version (ota status / health view).',
			'Start a second OTA transfer and kill the connection mid-transfer (e.g. device out of range or app closed briefly).',
			'Reconnect, log in again, and let the transfer resume.'
		],
		expected: [
			'Pre-flight warning states the device-reported battery level (or that it is unknown).',
			'Progress advances chunk-by-chunk during upload.',
			'After END: "Verifying and installing" then the expected disconnect is treated as success (presumed-success), not an error.',
			'After resume: BEGIN returns the device-held offset and the transfer continues from there, not from zero.',
			'The device boots the new firmware after completion.',
			'Behaviour fix vs the legacy app (review task 8.6): a chunk write that draws no response is reported as a timeout and retried from the same offset - the chunk size is NOT halved on a timeout, only on a genuine over-MTU write rejection.'
		]
	}
];
