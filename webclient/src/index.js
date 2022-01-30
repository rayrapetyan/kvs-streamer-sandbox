const express = require('express');
const path = require('path');
const { kinesis } = require('./kinesis');
require('dotenv').config();

const { json, urlencoded } = express;
const app = express();

app.use(json());
app.use(urlencoded({ extended: true }));
app.use(express.static(path.join(__dirname, 'static')));

app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

app.get('/kinesis-video-url', (req, res) => {
    kinesis.createChannel('KvsChannel', 'VIEWER', req.params['clientId']).then((response) => {
        return res.status(200).json(response);
    });
})

app.listen(process.env.LISTEN_PORT, () => {
    console.log(`app running on port ${process.env.LISTEN_PORT}`);
});
