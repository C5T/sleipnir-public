const users = ['alice', 'bob', 'charlie'];
const actions = ['read', 'write', 'admin'];
const objects = ['server123', 'server234', 'server345', 'database456', 'database567'];
for (let i = 0; i < 100000; ++i) {
  const user = users[Math.floor(Math.random() * users.length)];
  const action = actions[Math.floor(Math.random() * actions.length)];
  const object = objects[Math.floor(Math.random() * objects.length)];
  console.log(JSON.stringify({input:{user, action, object}}));
}
