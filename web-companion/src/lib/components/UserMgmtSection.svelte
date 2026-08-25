<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import * as um from '$lib/protocol/usermgmt';

	function onRefresh(): void {
		void session.listUsers();
	}

	function onDelete(userId: number): void {
		void session.deleteUser(userId);
	}
	function onRename(user: { id: number; name?: string }): void {
		void session.renameUser(user.id, user.name ?? '');
	}
	function onPermission(user: { id: number; perm: number }): void {
		void session.changePermission(user.id, user.perm);
	}
</script>

<section class="dashboard-section">
	<h3>User Management</h3>
	<p>
		List every enrolled user, and enrol, rename, re-permission or delete them.
		Every change requires an Admin fingerprint scan on the device to authorize
		it — a BLE request alone is never enough.
	</p>
	<button class="secondary-btn" onclick={onRefresh}>Refresh Users</button>

	<div class="users">
		{#if session.umUsers.length === 0}
			<p class="status-msg">No users enrolled.</p>
		{:else}
			<table>
				<thead>
					<tr><th>ID</th><th>Name</th><th>Permission</th><th>Actions</th></tr>
				</thead>
				<tbody>
					{#each session.umUsers as user (user.id)}
						<!-- The record stays in scope for each handler: no global
						     handler names, no values carried through markup
						     attributes. Svelte escapes the interpolated name. -->
						<tr>
							<td>{user.id}</td>
							<td>{#if user.name}{user.name}{:else}<em>—</em>{/if}</td>
							<td>{um.umPermissionName(user.perm)}</td>
							<td class="actions">
								<button class="secondary-btn" onclick={() => onRename(user)}>Rename</button>
								<button class="secondary-btn" onclick={() => onPermission(user)}>Permission</button>
								<button class="danger-btn" onclick={() => onDelete(user.id)}>Delete</button>
							</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{/if}
	</div>
	<p class="status-msg">{session.umStatus}</p>
</section>

<style>
	.users {
		margin-top: 0.75rem;
		overflow-x: auto;
	}
	table {
		border-collapse: collapse;
		width: 100%;
	}
	th,
	td {
		text-align: left;
		padding: 0.4rem 0.6rem;
		border-bottom: 1px solid var(--border);
	}
	.actions {
		display: flex;
		gap: 0.4rem;
		flex-wrap: wrap;
	}
</style>
