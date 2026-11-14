# Radio Presets

The radio presets are currently stored on the MeshCore App API server.

This allows us to update the presets without releasing new application updates.

The presets can be fetched from [https://api.meshcore.nz/api/v1/config](https://api.meshcore.nz/api/v1/config)

If you'd like a new radio preset to be added, this will need to be brought up for discussion in the [MeshCore Discord](https://meshcore.gg) server, or via the [issues section](https://github.com/meshcore-dev/MeshCore/issues) in our GitHub repo.

Before a new preset will be considered for being added, local radio regulations must be researched by the person suggesting the preset, and investigation of the frequencies with an SDR is advised to ensure the least interference to provide a decent user experience.

Once a preset is added, it's unlikely to be changed. Adding too many presets can lead to user frustration, as new users won't know which preset to select during setup.

At this time, presets provided by the MeshCore App API are just suggestions, but you are free to configure any radio settings through the companion applications, provided they comply with local laws/regulations.

> Note: We are planning to implement deep linking such as meshcore:// URLs, and QR codes so local mesh networks can easily distribute their own settings, without relying on the internet, or all the companion applications to provide all the settings.

## JSON Response

See the example JSON response below, which shows the expected format provided by the API server.

Your application should be programmed to expect any of the JSON keys to be missing from the response, as the format could change at any time without any notification.

Please configure an appropriate User Agent header string when sending your request to the server. We should be able to reach out to you if needed.

> Note: Some presets contain an optional `network_settings` section which defines the `path_hash_size` to use for this mesh network.

```json
{
  "config": {
    "suggested_radio_settings": {
      "info_message": "These presets are suggested by the community.",
      "entries": [
        {
          "title": "Australia (Narrow)",
          "description": "916.575MHz / SF7 / BW62.5 / CR7",
          "frequency": "916.575",
          "spreading_factor": "7",
          "bandwidth": "62.5",
          "coding_rate": "7"
        },
        {
          "title": "New Zealand (Narrow)",
          "description": "917.375MHz / SF7 / BW62.5 / CR5 / 2B",
          "frequency": "917.375",
          "spreading_factor": "7",
          "bandwidth": "62.5",
          "coding_rate": "5",
          "network_settings": {
            "path_hash_size": 2
          }
        },
        {
          "title": "Portugal 868",
          "description": "869.618MHz / SF7 / BW62.5 / CR6",
          "frequency": "869.618",
          "spreading_factor": "7",
          "bandwidth": "62.5",
          "coding_rate": "6"
        }
      ]
    }
  }
}
```