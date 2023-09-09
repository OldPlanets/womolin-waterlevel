<script>
	import { variables } from '$lib/utils/variables';
	import { onMount } from 'svelte';
	import { Button, FormGroup, Input, Label } from 'sveltestrap';
	import Fa from 'svelte-fa/src/fa.svelte';
	import { faSave } from '@fortawesome/pro-solid-svg-icons/faSave';
	import { faNetworkWired } from '@fortawesome/pro-solid-svg-icons/faNetworkWired';
	import { faWifi } from '@fortawesome/pro-solid-svg-icons/faWifi';
	import { faWind } from '@fortawesome/pro-solid-svg-icons/faWind';
	import { faCog } from '@fortawesome/pro-solid-svg-icons/faCog';
	import { faBroadcastTower } from '@fortawesome/pro-solid-svg-icons/faBroadcastTower';
	import { toast } from '@zerodevx/svelte-toast';

	let config = {};

	onMount(async () => {
		const response = await fetch(`/api/config`, {
			headers: { 'Content-type': 'application/json' }
		}).catch((error) => console.log(error));
		if (response.ok) config = await response.json();
		else {
			toast.push(`Error ${response.status} ${response.statusText}<br>Unable to receive current settings.`, variables.toast.error);
		}
	});

	async function doSaveSettings() {
		fetch(`/api/config`, {
			method: 'POST',
			body: JSON.stringify(config),
			headers: { 'Content-type': 'application/json' }
		})
			.then((response) => {
				if (response.ok) {
					toast.push(`Settings successfully saved`, variables.toast.success);
				} else {
					toast.push(`Error ${response.status} ${response.statusText}<br>Unable to store new AP configuration.`, variables.toast.error);
				}
			})
			.catch((error) => console.log(error));
	}
</script>

<svelte:head>
	<title>Sensor Settings</title>
</svelte:head>

<style>
	.form-group-section {
	  padding: 1rem;
	  margin: 1rem 0;
	  border-radius: 8px;
	  background-color: #f4f4f4;
	}

	.form-group-section h5 {
    	margin-bottom: 1rem;
  	}
  </style>
  
  <h4>Configuration</h4>
  <form on:submit|preventDefault={doSaveSettings}>
	<div class="form-group-section">
	  <h5><Fa icon={faNetworkWired}/> General</h5>
	  <FormGroup>
		<Label for="hostname">Hostname</Label>
		<Input id="hostname" bind:value={config.hostname} pattern="^[a-zA-Z][a-zA-Z\d\-]{'{'}1,30{'}'}[a-zA-Z\d]$" placeholder="Hostname" minlength="3" maxlength="32" style="margin-bottom: 0.7rem; margin-top: -0.3rem"/>
	  </FormGroup>
	</div>
  
	<div class="form-group-section">
	  <h5><Fa icon={faWifi}/> WiFi Settings</h5>
	  <FormGroup>
		<Input id="enableWifi" class="input-margin" bind:checked={config.enableWifi} type="checkbox" label="Enable WiFi" />
		<Label for="shutDownWifiMin">Turn off WiFi after startup in minutes (0 for always on)</Label>
		<Input id="shutDownWifiMin" bind:value={config.shutDownWifiMin} placeholder="0" min="0" max="65535" type="number" disabled={!config.enableWifi} style="margin-bottom: 0.7rem; margin-top: -0.3rem"/>
		<Input id="enableSoftAp" bind:checked={config.enableSoftAp} type="checkbox" label="Create AP if no WiFi is available" />
		<Label for="softAPPassword">Fallback Wifi AP Password</Label>
		<Input id="softApPassword" bind:value={config.softAPPassword} placeholder="AP Password" maxlength="32" disabled={!config.enableSoftAp} minlength={(config.softAPPassword && config.softAPPassword.length) > 0 ? 8 : 0} style="margin-bottom: 0.7rem; margin-top: -0.3rem"/>
		<Input id="enableBle" bind:checked={config.enableBle} type="checkbox" label="Enable Bluetooth (BLE)" />
		<Input id="enableDac" bind:checked={config.enableDac} type="checkbox" label="Enable DAC Analog Output" />
	  </FormGroup>
	</div>
  
	<div class="form-group-section">
	  <h5><Fa icon={faWind}/> Air Pump</h5>
	  <FormGroup>
		<Input id="autoAirPump" bind:checked={config.autoAirPump} type="checkbox" label="Enable automatic tube repressurizing" />
		<Label for="pressureThresh">Activate on pressure change in hPa</Label>
		<Input id="pressureThresh" bind:value={config.pressureThresh} placeholder="10" min="1" max="65535" type="number" style="margin-bottom: 0.7rem; margin-top: -0.3rem"/>
		<Input id="airPumpOnBoot" bind:checked={config.airPumpOnBoot} type="checkbox" label="Enable Air Pump on Power-On" />
	  </FormGroup>
	</div>
  
	<div class="form-group-section">
	  <h5><Fa icon={faCog}/> OTA Update</h5>
	  <FormGroup>
		<Label for="otaPassword">OTA (Over The Air) firmware update password</Label>
		<Input id="otaPassword" bind:value={config.otaPassword} placeholder="OTA Password" maxlength="32" style="margin-bottom: 0.7rem; margin-top: -0.3rem"/>
	  </FormGroup>
	</div>
  
	<div class="form-group-section">
	  <h5><Fa icon={faBroadcastTower}/> MQTT Settings</h5>
	  <FormGroup>
		<Input id="enableMqtt" bind:checked={config.enableMqtt} type="checkbox" label="Publish to MQTT Broker" />
		<Label for="mqttHost">MQTT Host or IP</Label>
		<Input id="mqttHost" bind:value={config.mqttHost} placeholder="mqtt.net.local" maxlength="32" style="margin-bottom: 0.7rem; margin-top: -0.3rem"/>
		<Label for="mqttPort">MQTT Port (default 1883)</Label>
		<Input id="mqttPort" bind:value={config.mqttPort} placeholder="1883" min="1" max="65535" type="number" style="margin-bottom: 0.7rem; margin-top: -0.3rem"/>
		<Label for="mqttTopic">MQTT Topic</Label>
		<Input id="mqttTopic" bind:value={config.mqttTopic} placeholder="some/sensor" maxlength="32" style="margin-bottom: 0.7rem; margin-top: -0.3rem"/>
		<Label for="mqttUser">MQTT Username</Label>
		<Input id="mqttUser" bind:value={config.mqttUser} placeholder="Username" maxlength="32" style="margin-bottom: 0.7rem; margin-top: -0.3rem"/>
		<Label for="mqttPass">MQTT Password</Label>
		<Input id="mqttPass" bind:value={config.mqttPass} placeholder="Password" maxlength="32" style="margin-bottom: 0.7rem; margin-top: -0.3rem"/>
	  </FormGroup>
	</div>
	<Button block style="height: 5rem;"><Fa icon={faSave} />&nbsp;Save Settings</Button>
  </form>
